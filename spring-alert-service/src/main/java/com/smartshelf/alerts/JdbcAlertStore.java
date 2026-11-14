package com.smartshelf.alerts;

import java.util.List;

import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Repository;

@Repository
final class JdbcAlertStore implements AlertStore {
    private final JdbcTemplate jdbc;

    JdbcAlertStore(JdbcTemplate jdbc) {
        this.jdbc = jdbc;
    }

    @Override
    public void save(String deviceId, long sourceTimestamp, String state, String advice) {
        jdbc.update("""
                INSERT INTO alert_event (device_id, source_timestamp, state, advice)
                VALUES (?, ?, ?, ?)
                ON DUPLICATE KEY UPDATE advice = ?
                """, deviceId, sourceTimestamp, state, advice, advice);
    }

    @Override
    public List<AlertRecord> recent(String deviceId, int limit) {
        return jdbc.query("""
                SELECT id, device_id, source_timestamp, state, advice
                FROM alert_event
                WHERE device_id = ?
                ORDER BY source_timestamp DESC, id DESC
                LIMIT ?
                """, (rs, rowNum) -> new AlertRecord(
                    rs.getLong("id"),
                    rs.getString("device_id"),
                    rs.getLong("source_timestamp"),
                    rs.getString("state"),
                    rs.getString("advice")), deviceId, limit);
    }
}
