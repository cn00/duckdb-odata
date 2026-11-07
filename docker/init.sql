CREATE TABLE IF NOT EXISTS customers (
    id BIGINT PRIMARY KEY,
    name VARCHAR NOT NULL,
    city VARCHAR,
    active BOOLEAN DEFAULT true
);

INSERT INTO customers VALUES
    (1, 'Alice', 'Berlin', true),
    (2, 'Bob', 'Paris', false),
    (3, 'Carol', 'London', true)
ON CONFLICT DO NOTHING;

CALL odata_expose('customers');
CALL odata_entity('customers', key := 'id');
