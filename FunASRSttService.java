package com.xiaozhi.ai.stt.providers;

import com.alibaba.fastjson.JSON;
import com.alibaba.fastjson.JSONObject;
import com.xiaozhi.ai.stt.SttResult;
import com.xiaozhi.ai.stt.SttService;
import com.xiaozhi.common.model.bo.ConfigBO;

import org.java_websocket.client.WebSocketClient;
import org.java_websocket.handshake.ServerHandshake;
import reactor.core.publisher.Flux;

import java.net.URI;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicReference;

import lombok.extern.slf4j.Slf4j;
/**
 * FunASR STT服务实现
 * <br/>
 * <a href="https://github.com/modelscope/FunASR/blob/main/runtime/docs/SDK_tutorial_online_zh.md">FunASR实时语音听写便捷部署教程</a>
 *  <br/>
 * <a href="https://github.com/modelscope/FunASR/blob/main/runtime/docs/SDK_advanced_guide_online_zh.md">FunASR实时语音听写服务开发指南</a>
 *  <br/>
 * <a href="https://www.funasr.com/static/offline/index.html">体验地址</a>
 */
@Slf4j
public class FunASRSttService implements SttService {

    private static final String PROVIDER_NAME = "funasr";

    private static final String SPEAKING_START = "{\"mode\":\"2pass\",\"wav_name\":\"voice.wav\",\"is_speaking\":true,\"wav_format\":\"pcm\",\"chunk_size\":[5,10,5],\"itn\":true}";
    private static final String SPEAKING_END = "{\"is_speaking\": false}";
    private static final int QUEUE_TIMEOUT_MS = 100; // 队列等待超时时间
    private static final long RECOGNITION_TIMEOUT_MS = 90000; // 识别总超时时间（90秒）
    private static final long FINAL_RESULT_WAIT_MS = 8000; // SPEAKING_END 发送后等待最终离线修正结果的最长时间（8秒）

    private final String apiUrl;

    public FunASRSttService(ConfigBO config) {
        this.apiUrl = config.getApiUrl();
    }

    @Override
    public String getProviderName() {
        return PROVIDER_NAME;
    }

    @Override
    public SttResult stream(Flux<byte[]> audioSink) {
        // 使用阻塞队列存储音频数据
        BlockingQueue<byte[]> audioQueue = new LinkedBlockingQueue<>();
        AtomicBoolean isCompleted = new AtomicBoolean(false);
        AtomicBoolean speakingEndSent = new AtomicBoolean(false);
        // 拼接所有2pass-offline离线修正结果
        StringBuilder offlineResult = new StringBuilder();
        AtomicReference<String> finalResult = new AtomicReference<>("");
        CountDownLatch recognitionLatch = new CountDownLatch(1);
        AtomicLong lastOfflineResultTime = new AtomicLong(System.currentTimeMillis());

        // 订阅Sink并将数据放入队列
        audioSink.subscribe(
            data -> audioQueue.offer(data),
            error -> {
                log.error("音频流处理错误", error);
                isCompleted.set(true);
            },
            () -> isCompleted.set(true)
        );

        // 创建WebSocket客户端
        WebSocketClient webSocketClient = new WebSocketClient(URI.create(apiUrl)) {
            @Override
            public void onOpen(ServerHandshake handshake) {
                log.debug("FunASR WebSocket连接已打开");
                send(SPEAKING_START);

                // 启动虚拟线程发送音频数据
                Thread.startVirtualThread(() -> {
                    try {
                        while (!isCompleted.get() || !audioQueue.isEmpty()) {
                            byte[] audioChunk = null;
                            try {
                                audioChunk = audioQueue.poll(QUEUE_TIMEOUT_MS, TimeUnit.MILLISECONDS);
                            } catch (InterruptedException e) {
                                log.warn("音频数据队列等待被中断", e);
                                Thread.currentThread().interrupt(); // 重新设置中断标志
                                break;
                            }

                            if (audioChunk != null && isOpen()) {
                                send(audioChunk);
                            }
                        }

                        // 发送结束信号
                        if (isOpen()) {
                            send(SPEAKING_END);
                            speakingEndSent.set(true);
                            log.debug("FunASR SPEAKING_END 已发送");
                        }
                    } catch (Exception e) {
                        log.error("发送音频数据时发生错误", e);
                    }
                });
            }

            @Override
            public void onMessage(String message) {
                try {
                    JSONObject jsonObject = JSON.parseObject(message);
                    boolean isFinal = Boolean.TRUE.equals(jsonObject.getBoolean("is_final"));
                    String mode = jsonObject.getString("mode");
                    String text = jsonObject.getString("text");
                    log.info("FunASR 原始响应: is_final={}, mode={}, text={}", isFinal, mode, text);
                    // 2pass模式：拼接每个离线修正片段（VAD可能将一句话分为多段）
                    if (isFinal && "2pass-offline".equals(mode)) {
                        if (text != null && !text.isEmpty()) {
                            // 使用同步块保证 offlineResult 拼接与读取的线程安全
                            synchronized (offlineResult) {
                                offlineResult.append(text);
                            }
                        }
                        lastOfflineResultTime.set(System.currentTimeMillis());
                        log.debug("FunASR 离线修正片段: {}", text);
                    }
                } catch (Exception e) {
                    log.error("解析FunASR响应失败", e);
                }
            }

            @Override
            public void onClose(int code, String reason, boolean remote) {
                log.info("FunASR WS关闭，原因：{}", reason);
                // 连接关闭时，离线修正结果已全部收到，设置最终结果
                synchronized (offlineResult) {
                    finalResult.set(offlineResult.toString());
                }
                recognitionLatch.countDown();
            }

            @Override
            public void onError(Exception ex) {
                log.error("FunASR WS错误", ex);
                // 先设置已有的结果，再释放锁，避免主线程读到空结果
                synchronized (offlineResult) {
                    finalResult.set(offlineResult.toString());
                }
                recognitionLatch.countDown();
            }
        };

        try {
            // 连接WebSocket
            webSocketClient.connect();

            // 等待识别完成。两种退出路径：
            // 1) FunASR 正常关闭连接 → onClose 触发，recognitionLatch 被释放
            // 2) 客户端已发送 SPEAKING_END，但 FunASR 未及时关闭 → 主动等待 FINAL_RESULT_WAIT_MS，
            //    若在该窗口内不再有新的离线修正片段，主动关闭连接并返回已有结果
            long deadline = System.currentTimeMillis() + RECOGNITION_TIMEOUT_MS;
            boolean recognized = false;
            while (System.currentTimeMillis() < deadline) {
                long remaining = deadline - System.currentTimeMillis();
                if (remaining <= 0) break;

                try {
                    recognized = recognitionLatch.await(Math.min(remaining, 500), TimeUnit.MILLISECONDS);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    break;
                }
                if (recognized) {
                    break;
                }

                // 若已发送 SPEAKING_END，且在 FINAL_RESULT_WAIT_MS 内没有新的离线修正片段，
                // 则认为 FunASR 已完成识别（不会再有结果），主动关闭连接并返回当前结果
                if (speakingEndSent.get()) {
                    long idleMs = System.currentTimeMillis() - lastOfflineResultTime.get();
                    if (idleMs >= FINAL_RESULT_WAIT_MS) {
                        String currentResult;
                        synchronized (offlineResult) {
                            currentResult = offlineResult.toString();
                            finalResult.set(currentResult);
                        }
                        log.info("FunASR 已 {}ms 无新离线结果，主动关闭连接并返回当前结果: '{}'",
                                idleMs, currentResult);
                        recognitionLatch.countDown();
                        recognized = true;
                        break;
                    }
                }
            }

            if (!recognized) {
                log.warn("FunASR识别超时");
            }
        } catch (Exception e) {
            log.error("FunASR识别过程中发生错误", e);
        } finally {
            // 关闭WebSocket连接
            if (webSocketClient.isOpen()) {
                webSocketClient.close();
            }
        }

        return SttResult.textOnly(finalResult.get());
    }
}