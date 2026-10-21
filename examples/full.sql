-- Démonstration complète des fonctionnalités de KernelDB
CREATE TABLE products (id INTEGER, name VARCHAR(50), price DOUBLE, stock BOOLEAN);

INSERT INTO products VALUES (1, 'Laptop', 999.99, true);
INSERT INTO products VALUES (2, 'Mouse', 19.99, true);
INSERT INTO products VALUES (3, 'Keyboard', 49.99, false);
INSERT INTO products VALUES (4, 'Monitor', 199.99, true);

-- Tri descendant par prix
SELECT * FROM products ORDER BY price DESC;

-- Filtre + projection + tri
SELECT name, price FROM products WHERE price > 50 ORDER BY name;

-- Mise à jour
UPDATE products SET price = 24.99 WHERE name = 'Mouse';
SELECT * FROM products WHERE name = 'Mouse';

-- Suppression
DELETE FROM products WHERE stock = false;
SELECT * FROM products ORDER BY id;
