package com.smartshelf.alerts;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import org.junit.jupiter.api.Test;

class AlertSyncServiceTest {
    @Test
    void skipsNormalAndInvalidRowsAndStoresAlerts() {
        ShelfSource source = new ShelfSource() {
            @Override
            public List<Map<String, Object>> analysis(String deviceId, int limit) {
                return List.of(
                        Map.of("timestamp", 100L, "state", "NORMAL"),
                        Map.of("timestamp", 101L, "state", "LOW_STOCK", "advice", "Refill"),
                        Map.of("timestamp", "bad", "state", "ALERT_SOUND"));
            }

            @Override
            public List<Map<String, Object>> telemetry(String deviceId, int limit) {
                return List.of();
            }
        };
        List<String> saved = new ArrayList<>();
        AlertStore store = new AlertStore() {
            @Override
            public void save(String deviceId, long timestamp, String state, String advice) {
                saved.add(deviceId + ":" + timestamp + ":" + state + ":" + advice);
            }

            @Override
            public List<AlertRecord> recent(String deviceId, int limit) {
                return List.of();
            }
        };

        new AlertSyncService(source, store).syncDevice("smart-shelf-02");

        assertEquals(List.of("smart-shelf-02:101:LOW_STOCK:Refill"), saved);
    }
}
