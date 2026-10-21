-- Exemple simple : créer une table et insérer des données
CREATE TABLE users (id INTEGER, name VARCHAR(50), age INTEGER);

INSERT INTO users VALUES (1, 'Alice', 30);
INSERT INTO users VALUES (2, 'Bob', 25);
INSERT INTO users VALUES (3, 'Charlie', 35);

SELECT * FROM users ORDER BY age;
SELECT name FROM users WHERE age > 26;

