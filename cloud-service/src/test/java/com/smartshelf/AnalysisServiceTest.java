package com.smartshelf;

import org.junit.jupiter.api.Test;

import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

class AnalysisServiceTest {
    @Test
    void shelf1ReportsEnvironmentalAlertAndInteractionRate() {
        Map<String, Object> result = AnalysisService.analyzeShelf1(
                "smart-shelf-01",
                1_700_000_000L,
                Map.of(
                        "temperatureC", 10.0,
                        "humidityPct", 50.0,
                        "pressureKPa", 101.3,
                        "interactionCount", 3,
                        "visitorCount", 4,
                        "customerPresent", true));

        assertEquals("ALERT_ENV", result.get("state"));
        assertTrue((Boolean) result.get("temperatureAlert"));
        assertFalse((Boolean) result.get("humidityAlert"));
        assertEquals(75.0, (Double) result.get("interactionRatePct"), 0.001);
    }

    @Test
    void shelf2PrioritizesManualAlarm() {
        Map<String, Object> result = AnalysisService.analyzeShelf2(
                "smart-shelf-02",
                1_700_000_000L,
                Map.of("buttonPressed", true, "lightRaw", 10, "soundRaw", 200, "weightG", 0));

        assertEquals("MANUAL_ALARM", result.get("state"));
        assertEquals(1, result.get("errorNumber"));
    }

    @Test
    void shelf2DetectsLowStock() {
        Map<String, Object> result = AnalysisService.analyzeShelf2(
                "smart-shelf-02",
                1_700_000_000L,
                Map.of("buttonPressed", false, "lightRaw", 500, "soundRaw", 20, "weightG", 12.5));

        assertEquals("LOW_STOCK", result.get("state"));
    }
}
