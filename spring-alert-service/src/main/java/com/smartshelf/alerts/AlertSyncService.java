package com.smartshelf.alerts;

import java.util.List;
import java.util.Map;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Service;

@Service
public class AlertSyncService {
    private static final Logger LOG = LoggerFactory.getLogger(AlertSyncService.class);
    static final List<String> DEVICE_IDS = List.of("smart-shelf-01", "smart-shelf-02");
    private final ShelfSource source;
    private final AlertStore store;

    AlertSyncService(ShelfSource source, AlertStore store) {
        this.source = source;
        this.store = store;
    }

    @Scheduled(fixedDelayString = "${shelf.sync-delay-ms}")
    public void syncAll() {
        for (String deviceId : DEVICE_IDS) {
            try {
                syncDevice(deviceId);
            } catch (RuntimeException error) {
                LOG.warn("Alert sync failed for {}: {}", deviceId, error.toString());
            }
        }
    }

    void syncDevice(String deviceId) {
        for (Map<String, Object> row : source.analysis(deviceId, 100)) {
            String state = String.valueOf(row.getOrDefault("state", ""));
            if (state.isBlank() || "NORMAL".equals(state)) continue;
            long timestamp = timestamp(row.get("timestamp"));
            if (timestamp <= 0) {
                LOG.warn("Skipped analysis row without valid timestamp for {}", deviceId);
                continue;
            }
            String advice = row.get("advice") == null ? null : row.get("advice").toString();
            store.save(deviceId, timestamp, state, advice);
        }
    }

    private static long timestamp(Object value) {
        if (value instanceof Number number) return number.longValue();
        try {
            return value == null ? -1 : Long.parseLong(value.toString());
        } catch (NumberFormatException ignored) {
            return -1;
        }
    }
}
