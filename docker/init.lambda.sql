CREATE TABLE IF NOT EXISTS lambda_customers (
    id BIGINT PRIMARY KEY,
    name VARCHAR NOT NULL,
    active BOOLEAN DEFAULT true
);

INSERT INTO lambda_customers VALUES
    (1, 'Lambda demo customer', true)
ON CONFLICT DO NOTHING;

CALL odata_expose('lambda_customers');
CALL odata_entity('lambda_customers', key := 'id');
