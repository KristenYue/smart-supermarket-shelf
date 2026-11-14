CREATE DATABASE IF NOT EXISTS smart_shelf CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE smart_shelf;

CREATE TABLE IF NOT EXISTS alert_event (
    id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    device_id VARCHAR(64) NOT NULL,
    source_timestamp BIGINT NOT NULL,
    state VARCHAR(32) NOT NULL,
    advice VARCHAR(255),
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uk_alert_source (device_id, source_timestamp, state),
    INDEX idx_alert_device_time (device_id, source_timestamp)
);
