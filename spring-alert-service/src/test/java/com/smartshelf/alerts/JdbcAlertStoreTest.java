package com.smartshelf.alerts;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.util.List;

import org.junit.jupiter.api.Test;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.jdbc.datasource.DriverManagerDataSource;

class JdbcAlertStoreTest {
    @Test
    void repeatedSyncUpdatesOneAlertAndQueriesByDevice() {
        DriverManagerDataSource datasource = new DriverManagerDataSource(
                "jdbc:h2:mem:alerts;MODE=MySQL;DB_CLOSE_DELAY=-1", "sa", "");
        JdbcTemplate jdbc = new JdbcTemplate(datasource);
        jdbc.execute("""
                CREATE TABLE IF NOT EXISTS alert_event (
                    id BIGINT AUTO_INCREMENT PRIMARY KEY,
                    device_id VARCHAR(64) NOT NULL,
                    source_timestamp BIGINT NOT NULL,
                    state VARCHAR(32) NOT NULL,
                    advice VARCHAR(255),
                    UNIQUE (device_id, source_timestamp, state)
                )
                """);
        JdbcAlertStore store = new JdbcAlertStore(jdbc);

        store.save("smart-shelf-01", 100, "ALERT_ENV", "Inspect");
        store.save("smart-shelf-01", 100, "ALERT_ENV", "Inspect again");
        store.save("smart-shelf-02", 100, "LOW_STOCK", "Refill");

        List<AlertRecord> rows = store.recent("smart-shelf-01", 20);
        assertEquals(1, rows.size());
        assertEquals("Inspect again", rows.get(0).advice());
        assertEquals(2, jdbc.queryForObject("SELECT COUNT(*) FROM alert_event", Integer.class));
    }
}
