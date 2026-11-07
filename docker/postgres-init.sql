CREATE TABLE products (
    id BIGINT PRIMARY KEY,
    name TEXT NOT NULL,
    price NUMERIC(10, 2) NOT NULL,
    in_stock BOOLEAN NOT NULL DEFAULT true
);

INSERT INTO products (id, name, price, in_stock) VALUES
    (1, 'DuckDB mug', 15.00, true),
    (2, 'OData guide', 9.50, true),
    (3, 'Extension sticker', 2.00, false);
