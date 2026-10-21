// Example: using KernelDB as an embedded library.
//
// This program demonstrates:
// - Opening a database
// - Creating a schema with constraints
// - Inserting with prepared statements (safe against SQL injection)
// - Querying with filters
// - Using transactions
// - Handling errors

#include "kerneldb/database.hpp"
#include "kerneldb/types.hpp"

#include <iomanip>
#include <iostream>
#include <string>

namespace {

void print_result(const kerneldb::QueryResult& result) {
    for (const auto& col : result.columns)
        std::cout << std::left << std::setw(20) << col;
    if (!result.columns.empty()) std::cout << '\n';

    for (const auto& row : result.rows) {
        for (const auto& value : row.values)
            std::cout << std::left << std::setw(20) << kerneldb::value_to_string(value);
        std::cout << '\n';
    }
    std::cout << result.message << '\n';
}

} // namespace

int main() {
    try {
        // 1. Open database
        kerneldb::Database db("example_data");
        std::cout << "Database opened at: " << db.data_directory().string() << "\n\n";

        // 2. Create schema (drop first for idempotence)
        if (db.has_table("users")) {
            db.execute("DROP TABLE users");
        }

        db.execute(
            "CREATE TABLE users ("
            "  id INTEGER PRIMARY KEY, "
            "  email VARCHAR(100) UNIQUE NOT NULL, "
            "  name VARCHAR(50) NOT NULL, "
            "  age INTEGER"
            ")");
        std::cout << "Table 'users' created.\n\n";

        // 3. Insert using prepared statements (safe against injection)
        {
            auto stmt = db.prepare("INSERT INTO users VALUES (?, ?, ?, ?)");

            struct User { int id; const char* email; const char* name; int age; };
            User users[] = {
                {1, "alice@example.com", "Alice",   30},
                {2, "bob@example.com",   "Bob",     25},
                {3, "carol@example.com", "Carol",   35},
            };

            for (const auto& u : users) {
                stmt.reset();
                stmt.bind_int(1, u.id);
                stmt.bind_string(2, u.email);
                stmt.bind_string(3, u.name);
                stmt.bind_int(4, u.age);
                stmt.execute(db);
            }
        }
        std::cout << "3 users inserted.\n\n";

        // 4. Query with filter and order
        std::cout << "--- Users older than 26, ordered by age ---\n";
        auto r1 = db.execute("SELECT name, age FROM users WHERE age > 26 ORDER BY age DESC");
        print_result(r1);
        std::cout << "\n";

        // 5. Constraint violation (demonstration)
        std::cout << "--- Trying to insert a duplicate email (should fail) ---\n";
        try {
            db.execute("INSERT INTO users VALUES (4, 'alice@example.com', 'Eve', 20)");
        } catch (const std::exception& e) {
            std::cout << "Expected error: " << e.what() << "\n";
        }
        std::cout << "\n";

        // 6. Transaction with rollback
        std::cout << "--- Transaction: insert then rollback ---\n";
        db.begin();
        db.execute("INSERT INTO users VALUES (10, 'dave@example.com', 'Dave', 40)");
        std::cout << "Dave inserted (in transaction).\n";

        auto during = db.execute("SELECT COUNT(*) FROM users");
        std::cout << "Count during transaction: "
                  << kerneldb::value_to_string(during.rows[0].values[0]) << "\n";

        db.rollback();
        std::cout << "Rolled back.\n";

        auto after = db.execute("SELECT COUNT(*) FROM users");
        std::cout << "Count after rollback: "
                  << kerneldb::value_to_string(after.rows[0].values[0]) << "\n\n";

        // 7. Aggregate queries
        std::cout << "--- Aggregates ---\n";
        auto stats = db.execute("SELECT COUNT(*), MIN(age), MAX(age), AVG(age) FROM users");
        print_result(stats);
        std::cout << "\n";

        // 8. Group by
        std::cout << "--- GROUP BY (age bracket) ---\n";
        db.execute("INSERT INTO users VALUES (4, 'dave@example.com', 'Dave', 30)");
        auto grouped = db.execute("SELECT age, COUNT(*) FROM users GROUP BY age ORDER BY age");
        print_result(grouped);

        std::cout << "\nDone.\n";
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
}



