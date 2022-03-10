#include "Grammar.h"

#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vcruntime.h>
#include <vector>

#include "src/common.h"
#include "src/grammar/GrammarReader.h"
#include "src/util/Formatter.h"
#include "src/display/steps.h"

using std::unordered_map;
using std::unordered_set;
using std::vector;

namespace gram {

Grammar::Grammar() {
    // Add built-in symbols
    epsilon = putSymbol(Constants::epsilon, true);
    addAlias(epsilon, "_e");
    addAlias(epsilon, "\\e");
    addAlias(epsilon, "\\epsilon");
    endOfInput = putSymbol(Constants::end_of_input, true);
}

auto Grammar::putSymbolNoDuplicate(Symbol &&sym) -> SymbolID {
    // `id` in sym is just the next id, we should check name instead
    auto it = idTable.find(sym.name);
    // Found
    if (it != idTable.end()) {
        auto &storedSym = symbolVector[it->second];
        if (storedSym.type == SymbolType::UNCHECKED) {
            storedSym.type = sym.type;
            return storedSym.id;
        }
        // This item attempts to overwrite symbol type
        if (sym.type != SymbolType::UNCHECKED) {
            if (!launchArgs.autoDefineTerminals && storedSym.type != sym.type) {
                throw std::runtime_error(
                    "Redefinition of previous symbol with different types");
            } else if (launchArgs.autoDefineTerminals &&
                       sym.type == SymbolType::NON_TERM) {
                // If auto-define is enabled, a symbol can upgrade to
                // non-terminal.
                storedSym.type = SymbolType::NON_TERM;
            }
        }

        return storedSym.id;
    }
    // Not found
    // Do not rely on `id` in arg `sym`
    auto symid = static_cast<SymbolID>(symbolVector.size());
    idTable.emplace(sym.name, symid);
    // Use `move` after sym.name is copyed to idTable
    symbolVector.push_back(std::move(sym));
    return symid;
}

auto Grammar::putSymbol(const char *name, bool isTerm) -> SymbolID {
    Symbol sym{isTerm ? SymbolType::TERM : SymbolType::NON_TERM,
               static_cast<SymbolID>(symbolVector.size()), name};
    return putSymbolNoDuplicate(std::move(sym));
}

// Delays checking
auto Grammar::putSymbolUnchecked(const char *name) -> SymbolID {
    Symbol sym{SymbolType::UNCHECKED,
               static_cast<SymbolID>(symbolVector.size()), name};
    return putSymbolNoDuplicate(std::move(sym));
}

void Grammar::addAlias(SymbolID id, const char *alias) {
    if (symbolVector.size() <= static_cast<size_t>(id)) {
        throw std::runtime_error("No such symbol: " + std::to_string(id));
    }
    idTable.emplace(alias, id);
}

ProductionID Grammar::addProduction(SymbolID leftSymbol,
                                    std::vector<SymbolID> rightSymbols) {
    // Production ID
    auto id = (ProductionID)productionTable.size();
    productionTable.emplace_back(leftSymbol, std::move(rightSymbols));
    symbolVector[leftSymbol].productions.push_back(id);
    return id;
}

ProductionTable const &Grammar::getProductionTable() const {
    return productionTable;
}

std::string Grammar::dump() const {
    std::string s;
    util::Formatter f;

    s += "Symbols:\n";
    auto const &vec = symbolVector;
    for (size_t i = 0; i < vec.size(); ++i) {
        s += f.formatView(
            "    %zd) %s %s", i, vec[i].name.c_str(),
            vec[i].type == SymbolType::TERM ? "[TERM" : "[NONTERM");
        if (vec[i].id == getStartSymbol().id) {
            s += ",START";
        }
        s += "]\n";
        stepSymbol(i, vec[i].name.c_str(), vec[i].type == SymbolType::TERM,
                   vec[i].id == getStartSymbol().id);
    }

    s += "Productions:";
    // int productionCounter = 0;
    auto const &table = productionTable;
    auto table_size = table.size();
    // for (auto &production : productionTable) {
    for (size_t i = 0; i < table_size; ++i) {
        s += "\n    ";
        s += std::to_string(i);
        s += ") ";
        s += vec[table[i].leftSymbol].name;
        s += " ->";
        for (auto sym : table[i].rightSymbols) {
            s += ' ';
            s += vec[sym].name;
        }
        stepProduction(i, table[i].leftSymbol,
                       (const int *)table[i].rightSymbols.data(),
                       table[i].rightSymbols.size());
    }

    return s;
}

auto Grammar::fromFile(const char *fileName) -> Grammar {
    std::fstream stream(fileName, std::ios::in);
    if (!stream.is_open()) {
        throw std::runtime_error(std::string("File not found: ") + fileName);
    }
    auto g = GrammarReader::parse(stream);
    stepPrepare((int)g.getAllSymbols().size(),
                (int)g.getProductionTable().size());
    display(GRAMMAR_RULES, INFO, "Grammar rules has been parsed", &g);
    g.resolveSymbolAttributes();
    return g;
}

auto Grammar::fromStdin() -> Grammar {
    return GrammarReader::parse(::std::cin).resolveSymbolAttributes();
}

void Grammar::checkViolations() {
    // Check if there's a sym with no type
    for (auto &sym : symbolVector) {
        if (sym.type == SymbolType::UNCHECKED) {
            throw UnsolvedSymbolError(sym);
        }
    }
    // TODO: check if there's a A -> A
}

// setStart() should only be called once.
void Grammar::setStart(const char *name) {
    // Although we know start symbol must not be a terminal,
    // we cannot define it here, we need to check symbol later.
    start = putSymbolUnchecked(name);

    // Since this is the start, there are no productions yet.
}

void Grammar::collectNonterminals() {
    auto sz = static_cast<int>(symbolVector.size());
    nonterminals.reserve(sz);
    attrTableLineMap = std::vector<int>(sz, -1);

    for (int i = 0; i < sz; ++i) {
        if (symbolVector[i].type == SymbolType::NON_TERM) {
            nonterminals.push_back(i);
            attrTableLineMap[i] = (int)nonterminals.size();
        }
    }
}

// This function needs to get a Symbol & from symVec,
// so symVec must be a mutable reference
bool Grammar::resolveNullable(Symbol &sym) {
    if (sym.nullable.has_value()) {
        return sym.nullable.value();
    }

    // Epsilon is nullable
    if (sym.id == epsilon) {
        sym.nullable.emplace(true);
        stepNullable(sym.id, true, "Epsilon is nullable");
        return true;
    }

    // Place false first to prevent infinite recursive calls
    sym.nullable.emplace(false);

    // For t in T, t is not nullable
    if (sym.type == SymbolType::TERM) {
        stepNullable(sym.id, false,
                     "Terminals other than epsilon are not nullable");
        return false;
    }

    // For A -> a...b, A is nullable <=> a ... b are all nullable
    for (auto const &prodID : symbolVector[sym.id].productions) {
        auto const &production = productionTable[prodID];
        // This symbol has epsilon production
        if (production.rightSymbols.empty()) {
            sym.nullable.emplace(true);
            stepNullable(sym.id, true, "The symbol has an epsilon production");
            return true;
        }
        bool prodNullable = true;
        for (int rid : production.rightSymbols) {
            if (!resolveNullable(symbolVector[rid])) {
                prodNullable = false;
                break;
            }
        }
        if (prodNullable) {
            sym.nullable.emplace(true);
            stepNullable(
                sym.id, true,
                "All components in a production of the symbol are nullable");
            return true;
        }
        // No luck, process the next production
    }

    // stepcode("%s %d 1 false\n", Constants::table_put, attrTableLineMap[sym.id]);
    stepNullable(sym.id, false, "All productions of the symbol are not nullable");
    return false;
}

// This function needs to get a Symbol & from symVec,
// so symVec must be a mutable reference
void Grammar::resolveFirstSet(vector<int> &visit, Symbol &curSymbol) {
    if (visit[curSymbol.id])
        return;

    // Mark the flag to prevent circular recursive call
    visit[curSymbol.id] = 1;

    for (auto const &prodID : symbolVector[curSymbol.id].productions) {
        auto const &production = productionTable[prodID];
        // This flag is true when the body of this production is nullable
        bool allNullable = true;
        for (int rid : production.rightSymbols) {
            auto &rightSymbol = symbolVector[rid];
            if (!visit[rid]) {
                resolveFirstSet(visit, rightSymbol);
            }
            for (auto sid : rightSymbol.firstSet) {
                if (sid != epsilon) {
                    curSymbol.firstSet.insert(sid);
                    stepFirstAdd(
                        curSymbol.id, sid,
                        "For A -> ab...xy..., y's first set is a subset "
                        "of A's when ab...x are all nullable");
                }
            }
            if (!rightSymbol.nullable.value()) {
                allNullable = false;
                break;
            }
        }
        if (allNullable) {
            curSymbol.firstSet.insert(epsilon);
            stepFirstAdd(
                curSymbol.id, epsilon,
                "The first set of a nullable symbol contains epsilon^");
        }
    }
}

// This function figures out the dependencies among Follow sets.
void Grammar::resolveFollowSet(
    vector<int> &visit,
    unordered_map<SymbolID, unordered_set<SymbolID>> &dependencyTable,
    std::pair<const SymbolID, std::unordered_set<SymbolID>> &dependency) {
    if (visit[dependency.first]) {
        return;
    }
    visit[dependency.first] = 1;

    auto &parentSet = dependency.second;
    for (SymbolID parent : parentSet) {
        auto it = dependencyTable.find(parent);
        if (it != dependencyTable.end() && !it->second.empty()) {
            resolveFollowSet(visit, dependencyTable, *it);
        }
        // Add follow set items from parent
        auto const &parentFollowSet = symbolVector[parent].followSet;
        auto &followSet = symbolVector[dependency.first].followSet;
        followSet |= parentFollowSet;
        stepFollowMerge(dependency.first, parent,
                        "For A -> ...abc...z, and bc...z are all nullable, A's "
                        "follow set is a subset of a's");
    }
    parentSet.clear();
};

Grammar &Grammar::resolveSymbolAttributes() {
    collectNonterminals();

    stepPrintf("# Grammar file is read\n");

    // Nullable
    // Epsilon is nullable // This is done in resolveNullable
    // symbolVector[epsilon].nullable.emplace(true);

    // Apply 2 more rules:
    // For t in T, t is not nullable
    // For A -> a...b, A is nullable <=> a ... b are all nullable
    for (auto &symbol : symbolVector) {
        resolveNullable(symbol);
    }

    // display(SYMBOL_TABLE, INFO, "Calculate nullables", this);

    // First Set

    vector<int> visit(symbolVector.size(), 0);
    // For t in T, First(t) = {t}
    for (auto &symbol : symbolVector) {
        if (symbol.type == SymbolType::TERM) {
            symbol.firstSet.insert(symbol.id);
            visit[symbol.id] = 1;
            stepFirstAdd(symbol.id, symbol.id,
                         "The first set of a terminal contains itself");
        }
    }

    // For a in T or N, if a -*-> epsilon, then epsilon is in First(a)
    for (auto &symbol : symbolVector) {
        if (symbol.nullable.value()) {
            symbol.firstSet.insert(epsilon);
            stepFirstAdd(
                symbol.id, epsilon,
                "The first set of any nullable symbol contains epsilon");
        }
    }

    // For n in T, check production chain
    for (auto &symbol : symbolVector) {
        resolveFirstSet(visit, symbol);
    }

    // display(SYMBOL_TABLE, INFO, "Calculate first set", this);

    // Follow Set

    // Add $ to Follow set of the start symbol
    symbolVector[start].followSet.insert(endOfInput);
    stepFollowAdd(start, endOfInput,
                  "The follow set of start symbol contains end-of-input");

    // Get symbols from the next symbol's First set, and generate
    // a dependency graph.
    // e.g. table[a] = {b, c} ===> a needs b and c
    unordered_map<SymbolID, unordered_set<SymbolID>> dependencyTable;

    for (auto const &symbol : symbolVector) {
        // If this for-loop is entered, the symbol cannot be a
        // terminal.
        for (auto prodID : symbol.productions) {
            auto const &prod = productionTable[prodID];
            auto const &productionBody = prod.rightSymbols;
            // Skip epsilon productions
            if (productionBody.empty())
                continue;

            auto const &last = symbolVector[productionBody.back()];

            // Only calculate Follow sets for non-terminals
            if (last.type == SymbolType::NON_TERM)
                dependencyTable[last.id].insert(prod.leftSymbol);

            long size = static_cast<long>(productionBody.size());
            for (long i = size - 2; i >= 0; --i) {
                // Only calculate Follow sets for non-terminals
                auto &thisSymbol = symbolVector[productionBody[i]];
                auto const &nextSymbol = symbolVector[productionBody[i + 1]];

                if (thisSymbol.type != SymbolType::NON_TERM) {
                    continue;
                }

                for (auto first : nextSymbol.firstSet) {
                    if (first != epsilon) {
                        thisSymbol.followSet.insert(first);
                        stepFollowAdd(
                            thisSymbol.id, first,
                            "For A -> ...ab..., all non-epsilon symbols "
                            "in b's first set appear in a's follow set");
                    }
                }
                if (nextSymbol.nullable.value())
                    dependencyTable[thisSymbol.id].insert(nextSymbol.id);
            }
        }
    }

    // Figure out dependencies
    std::fill(visit.begin(), visit.end(), 0);
    for (auto &entry : dependencyTable) {
        resolveFollowSet(visit, dependencyTable, entry);
    }

    display(SYMBOL_TABLE, INFO, "Calculate symbol attributes", this);

    // stepcode("%s Attributes\n", Constants::mark_end);

    return *this;
}

static std::string dumpSymbolSet(Grammar const &g,
                                 Symbol::SymbolSet const &symset) {
    std::string s = "{";
    auto const &symvec = g.getAllSymbols();
    for (auto symid : symset) {
        s += ' ';
        s += symvec[symid].name;
    }
    s += (s.size() == 1) ? "}" : " }";
    return s;
}

std::string Grammar::dumpNullable(const Symbol &symbol) {
    if (!symbol.nullable.has_value()) {
        return "?";
    }
    return symbol.nullable.value() ? "true" : "false";
}

std::string Grammar::dumpFirstSet(const Symbol &symbol) const {
    return dumpSymbolSet(*this, symbol.firstSet);
}

std::string Grammar::dumpFollowSet(const Symbol &symbol) const {
    return dumpSymbolSet(*this, symbol.followSet);
}

std::string Grammar::dumpProduction(ProductionID prodID) const {
    std::string s;
    auto const &production = productionTable[prodID];
    s += symbolVector[production.leftSymbol].name;
    s += " ->";
    for (int rightSymbol : production.rightSymbols) {
        s += ' ';
        s += symbolVector[rightSymbol].name;
    }
    return s;
}

const Symbol &Grammar::getEndOfInputSymbol() const {
    return symbolVector[endOfInput];
}

const Symbol &Grammar::getEpsilonSymbol() const {
    return symbolVector[epsilon];
}

const Symbol &Grammar::getStartSymbol() const { return symbolVector[start]; }

const Grammar::symvec_t &Grammar::getAllSymbols() const { return symbolVector; }

Symbol const &Grammar::findSymbol(std::string const &s) const {
    auto it = idTable.find(s);
    if (it != idTable.end())
        return symbolVector[it->second];
    throw NoSuchSymbolError(s);
}

} // namespace gram
