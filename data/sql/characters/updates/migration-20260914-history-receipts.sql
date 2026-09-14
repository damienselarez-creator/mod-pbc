-- Apply to the characters database before starting this version of mod-pbc.
-- Receipts intentionally outlive deleted/condensed history. Never cascade or
-- clear them during a character reset: an old journal must not resurrect data.
CREATE TABLE IF NOT EXISTS mod_pbc_history_receipts (
    token CHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL PRIMARY KEY,
    history_id BIGINT UNSIGNED NULL
) ENGINE=InnoDB;
