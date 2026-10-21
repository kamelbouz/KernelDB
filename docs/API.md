# KernelDB — API C++

État documentaire : 20 octobre 2026. La version déclarée par CMake et le shell
reste **1.7.0** ; les évolutions J4 à J5.5 sont décrites comme travaux non publiés.
Cette mise à jour repose sur les sources locales et ne constitue pas un nouvel audit.

## Intégration

```cmake
find_package(kerneldb CONFIG REQUIRED)
target_link_libraries(mon_application PRIVATE kerneldb::core)
```

Installer préalablement le projet avec `cmake --install build --prefix <dossier>`
(ajouter `--config Release` si nécessaire), puis renseigner `CMAKE_PREFIX_PATH`.
Dans le dépôt, `add_subdirectory` permet aussi de lier `kerneldb::core`.

```cpp
#include "kerneldb/database.hpp"
#include "kerneldb/session.hpp"
#include "kerneldb/types.hpp"
#include <iostream>

int main() {
    kerneldb::Database db("demo_api");
    if (!db.has_table("users")) {
        db.execute("CREATE TABLE users (id INTEGER PRIMARY KEY, name VARCHAR(50))");
        db.execute("INSERT INTO users VALUES (1, 'Alice')");
    }
    auto stmt = db.prepare("SELECT name FROM users WHERE id = ?");
    stmt.bind_int(1, 1);
    auto result = stmt.execute(db);
    for (const auto& row : result.rows)
        std::cout << kerneldb::value_to_string(row.values[0]) << '\n';
}
```

## Database

`Database` ouvre une base dans un répertoire précis et détient son verrou exclusif.
Elle ne gère pas la racine multi-base : utiliser `Session` pour cela.

```cpp
struct DatabaseOptions { std::size_t page_size = PAGE_SIZE_DEFAULT; };
explicit Database(const std::filesystem::path& directory, DatabaseOptions opts = {});
QueryResult execute(const std::string& sql);
QueryResult execute_statement(const Statement& statement);
QueryResult execute_top_level(const TopLevelStatement& stmt);
PreparedStatement prepare(const std::string& sql);
void begin();
void commit();
void rollback();
bool in_transaction() const;
TxState tx_state() const;
```

`TxState` vaut `None`, `Active` ou `Aborted`. `in_transaction()` ne vaut vrai
que pour `Active` ; consulter `tx_state()` pour distinguer `Aborted` de `None`.
Une erreur peut faire passer une transaction active à Aborted ; il faut alors
un rollback. Les DDL sont refusés dans une transaction explicite.

Accesseurs : `table_names()`, `all_schemas()`, `has_table(name)`,
`schema_of(name)`, `data_directory()`, `options()`, `index_manager()`.
La référence rendue par `schema_of` dépend de la durée de vie et des modifications
du catalogue ; éviter de la conserver à travers des DDL.

## PreparedStatement

Déclarée dans `database.hpp` ; il n'existe pas de header séparé requis.
Les paramètres `?` sont numérotés à partir de **1**.

| Méthode | Usage |
|---|---|
| `parameter_count()`, `bound_count()`, `is_fully_bound()` | État des paramètres |
| `bind_null(i)` | Lier NULL |
| `bind_int(i, int64)`, `bind_double(i, double)` | Valeurs numériques |
| `bind_bool(i, bool)`, `bind_string(i, string)` | Booléen et chaîne |
| `bind_value(i, Value)` | Valeur générique |
| `reset()` | Effacer les liaisons avant réutilisation |
| `execute(Database&)` | Exécuter avec les paramètres liés |
| `execute_with(Database&, const std::vector<Value>&)` | Fournir les valeurs à l'appel |

Les paramètres représentent des valeurs, pas des noms de tables ou colonnes.
La sélection d'un accès indexé utilise les valeurs substituées.

## Session

```cpp
kerneldb::Session session("mes_bases");
if (!session.has_database("demo")) session.create_database("demo");
session.use("demo");
auto result = session.execute("SHOW DATABASES");
```

Constructeur : `explicit Session(const std::filesystem::path& data_dir = {})`.
Sans chemin explicite, résolution par `KERNELDB_DATA_DIR`, puis `.kerneldb` dans
le profil utilisateur. Méthodes : `create_database(name, opts = {})`,
`drop_database(name)`, `use(name)`, `has_active()`, `active_name()`, `active()`,
`database(name)`, `has_database(name)`, `database_names()`, `execute(sql)`, `data_dir()`.
`Session` assure le routage des références qualifiées `base.table`.
Il n'existe pas de transaction atomique multi-base documentée.

## Résultats et types

```cpp
struct QueryResult {
    std::vector<std::string> columns;
    std::vector<Row> rows;
    std::string message;
};
struct Row { std::vector<Value> values; };
```

`Value` est un `std::variant<std::monostate, std::int64_t, double, bool,
std::string, Parameter>`. `std::monostate` représente NULL ; `Parameter` sert
aux placeholders de l'AST. `value_to_string` fournit une représentation d'affichage.

`Column` contient `name`, `type`, `max_length`, `is_not_null`.
Les anciennes propriétés PK/UNIQUE par colonne ont été remplacées dans le schéma :
`TableSchema` contient `name`, `columns`, `primary_key`, `unique_constraints`, `indexes`.
`UniqueConstraint` contient `name` et `columns`.
Le champ `indexes` et le type `IndexDef` ne signifient pas que `CREATE INDEX` SQL
est disponible.

## Stockage et index : interfaces internes exposées

L'application embarquée devrait passer par `Database` afin de conserver validation,
contraintes et maintenance des index. Les opérations bas niveau suivantes peuvent
contourner ces garanties si elles sont employées directement.

```cpp
struct RowId { std::uint32_t page_id; std::uint16_t slot_id; };
std::optional<Row> StorageManager::read_by_id(const TableSchema&, const RowId&) const;
RowId StorageManager::insert(const TableSchema&, const Row&) const;
RowId StorageManager::update_by_id(const TableSchema&, const RowId&, const Row&) const;
void StorageManager::delete_by_id(const TableSchema&, const RowId&) const;
```

`read_by_id` passe par le cache et retourne `nullopt` pour une page ou un slot
absent ; les erreurs d'E/S ou de décodage ne sont pas converties en absence.
`update_by_id` peut retourner un nouveau RowId : un RowId n'est pas un identifiant
métier permanent, notamment après relocalisation ou réécriture.

```cpp
struct IndexLookupResult {
    bool used_index = false;
    std::optional<RowId> rid;
};
IndexLookupResult IndexManager::lookup_equal(const TableSchema&, const WhereExpr&) const;
```

Trois résultats : `used_index=false` demande un scan ; `true` sans `rid` indique
une clé absente ; `true` avec `rid` fournit l'adresse à lire. `Database` vérifie
ensuite le WHERE entier et refuse un RowId d'index absent du stockage.

Une égalité sur toutes les colonnes d'une `PRIMARY KEY` ou d'une contrainte
`UNIQUE` peut utiliser le B+ tree, puis lire directement la ligne par son `RowId`.
Les égalités peuvent être réunies par `AND`, dans un ordre différent de celui
des colonnes de la clé. La PK est essayée avant les contraintes UNIQUE.

Le chemin indexé exige des valeurs non NULL de même type que les colonnes
(et des doubles finis). Une clé composite partielle, un `OR` dans le filtre,
ou une conversion numérique nécessaire provoque un repli vers le scan.
Les plages, `IN` et `LIKE` ne constituent pas des accès indexés à ce stade ;
ils peuvent rester des filtres supplémentaires avec une égalité de clé complète.
Le filtre WHERE entier est réévalué après lecture. Une clé absente donne un
résultat vide sans scan ; un index pointant vers une ligne absente déclenche une erreur.

## Erreurs et garanties

Les erreurs SQL et de stockage sont généralement signalées par `std::runtime_error`.
Le texte des messages n'est pas un code d'erreur stable. L'API ne garantit pas
l'utilisation concurrente des objets. Les transactions disposent d'un journal
persistant, avec les limites de récupération et de durabilité suivies dans
[le changelog](../CHANGELOG.md). Consulter aussi [le guide](USAGE.md).
