#pragma once
#include "kerneldb/tokenizer.hpp"
#include "kerneldb/types.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace kerneldb {

// ============================================================================
// Module de validation et normalisation — contrats partagés
//
// Centralise les règles qui étaient dupliquées entre CREATE/ALTER, INSERT/UPDATE,
// et les différents points de parsing. Inspiré du RDS de System R, qui validait
// les schémas et normalisait les données avant toute opération.
// ============================================================================

// ---------- Validation de schéma ----------

// Vérifie qu'un schéma est valide :
//  - pas de colonne dupliquée (noms comparés en minuscules)
//  - au plus une PRIMARY KEY
//  - longueurs VARCHAR > 0 et raisonnables
//  - types de colonnes cohérents
// Lève std::runtime_error avec un message explicite si invalide.
void validate_schema(const TableSchema& schema);

// ---------- Parsing de taille ----------

// Parse un token comme entier non négatif.
//  - refuse les signes (+/-)
//  - refuse les tokens partiellement numériques ("1.2", "1abc")
//  - refuse les valeurs hors plage
//  - vérifie que le token est entièrement consommé
// `context` est utilisé dans le message d'erreur ("LIMIT", "VARCHAR", ...)
// `max_value` borne supérieure (0 = pas de borne)
std::uint64_t parse_size_token(const Token& tok,
                                const std::string& context,
                                std::uint64_t max_value = 0);

// ---------- Normalisation de valeur ----------

// Normalise une valeur selon le type de la colonne cible.
//  - INT64 stocké en INT64 (pas de conversion)
//  - DOUBLE stocké en DOUBLE (conversion si INT64 avec perte potentielle)
//  - VARCHAR stocké en STRING
//  - BOOLEAN stocké en BOOL
//  - NULL reste NULL
// Lève std::runtime_error si la conversion est impossible.
Value normalize_value(const Value& v, const Column& col);

// Normalise toutes les valeurs d'une ligne selon le schéma.
Row normalize_row(const Row& row, const TableSchema& schema);

// ---------- Validation de contraintes ----------

// Vérifie les contraintes (NOT NULL, UNIQUE, PRIMARY KEY) sur une ligne
// DÉJÀ NORMALISÉE. Les valeurs existantes doivent aussi être normalisées.
void validate_constraints(const TableSchema& schema,
                           const Row& normalized_row,
                           const std::vector<Row>& existing_normalized);
// J5.4-D : validation NOT NULL seule (O(1) par ligne).
// Les contraintes PK/UNIQUE sont vérifiées via l'index (IndexManager).
void validate_not_null(const TableSchema& schema, const Row& normalized_row);

} // namespace kerneldb
