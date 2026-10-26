#include <SFML/Graphics.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace
{
const sf::Color Background(10, 15, 24);
const sf::Color Panel(18, 27, 40);
const sf::Color PanelLight(24, 36, 52);
const sf::Color Cyan(69, 211, 255);
const sf::Color Green(80, 220, 150);
const sf::Color Red(255, 100, 110);
const sf::Color Muted(145, 164, 185);

struct Formula
{
    enum Type { Atom, Not, And, Or, Nor, Xor, Imp, Iff };
    Type type;
    std::string atom;
    Formula* left;
    Formula* right;
    Formula(Type t, const std::string& a = "") : type(t), atom(a), left(nullptr), right(nullptr) {}
};

struct Rule
{
    std::string original;
    Formula* formula;
    std::string formal;
};

struct Model
{
    std::map<std::string, bool> values;
};

struct TableauBranch
{
    std::vector<std::pair<std::string, bool>> literals;
    bool closed;
};

std::string trim(const std::string& text)
{
    size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string replaceAll(std::string text, const std::string& from, const std::string& to)
{
    size_t position = 0;
    while ((position = text.find(from, position)) != std::string::npos)
    {
        text.replace(position, from.size(), to);
        position += to.size();
    }
    return text;
}

std::string cleanAtom(std::string text)
{
    text = trim(text);
    text = lower(text);
    const std::string prefixes[] = {"el ", "la ", "los ", "las ", "un ", "una ", "que "};
    for (const std::string& prefix : prefixes)
    {
        if (text.compare(0, prefix.size(), prefix) == 0) text = trim(text.substr(prefix.size()));
    }
    while (!text.empty() && std::ispunct(static_cast<unsigned char>(text.back())))
        text.pop_back();
    return trim(text);
}

Formula* make(Formula::Type type, Formula* left = nullptr, Formula* right = nullptr,
              const std::string& atom = "")
{
    Formula* result = new Formula(type, atom);
    result->left = left;
    result->right = right;
    return result;
}

Formula* atomFormula(const std::string& value)
{
    return make(Formula::Atom, nullptr, nullptr, cleanAtom(value));
}

Formula* parseClause(const std::string& clause)
{
    std::string text = cleanAtom(clause);
    if (text.empty()) return atomFormula("proposición vacía");
    if (text.compare(0, 2, "o ") == 0) text = trim(text.substr(2));
    if (text.compare(0, 3, "ni ") == 0)
    {
        size_t niPos = text.find(" ni ", 3);
        if (niPos != std::string::npos)
        {
            Formula* left = parseClause(text.substr(3, niPos - 3));
            Formula* right = parseClause(text.substr(niPos + 4));
            return make(Formula::Nor, left, right);
        }
    }
    const std::string nandPrefix = "no es cierto que ";
    if (text.compare(0, nandPrefix.size(), nandPrefix) == 0)
    {
        std::string conjunction = text.substr(nandPrefix.size());
        const std::string suffix = " al mismo tiempo";
        if (conjunction.size() > suffix.size() &&
            conjunction.compare(conjunction.size() - suffix.size(), suffix.size(), suffix) == 0)
            conjunction = trim(conjunction.substr(0, conjunction.size() - suffix.size()));
        size_t andPos = conjunction.find(" y ");
        if (andPos != std::string::npos)
        {
            Formula* left = parseClause(conjunction.substr(0, andPos));
            Formula* right = parseClause(conjunction.substr(andPos + 3));
            return make(Formula::Not, make(Formula::And, left, right));
        }
        size_t orPos = conjunction.find(" o ");
        if (orPos != std::string::npos)
        {
            Formula* left = parseClause(conjunction.substr(0, orPos));
            Formula* right = parseClause(conjunction.substr(orPos + 3));
            return make(Formula::Nor, left, right);
        }
    }
    if (text.compare(0, 3, "no ") == 0)
        return make(Formula::Not, atomFormula(text.substr(3)));
    size_t internalNot = text.find(" no ");
    if (internalNot != std::string::npos &&
        text.find(" y ") == std::string::npos &&
        text.find(" o ") == std::string::npos)
    {
        std::string positive = trim(text.substr(0, internalNot) + " " +
                                    text.substr(internalNot + 4));
        return make(Formula::Not, atomFormula(positive));
    }

    size_t andPos = text.find(" y ");
    if (andPos != std::string::npos)
        return make(Formula::And, parseClause(text.substr(0, andPos)),
                    parseClause(text.substr(andPos + 3)));
    const std::string exclusiveSuffix = " pero no ambos";
    size_t exclusivePos = text.find(exclusiveSuffix);
    size_t orPos = text.find(" o ");
    if (exclusivePos != std::string::npos && orPos != std::string::npos &&
        orPos < exclusivePos)
    {
        return make(Formula::Xor, parseClause(text.substr(0, orPos)),
                    parseClause(text.substr(orPos + 3,
                                            exclusivePos - (orPos + 3))));
    }
    if (orPos != std::string::npos)
        return make(Formula::Or, parseClause(text.substr(0, orPos)),
                    parseClause(text.substr(orPos + 3)));
    return atomFormula(text);
}

std::string formulaText(const Formula* formula, const std::map<std::string, char>& names)
{
    if (!formula) return "";
    if (formula->type == Formula::Atom) return std::string(1, names.at(formula->atom));
    if (formula->type == Formula::Not) return "~" + formulaText(formula->left, names);
    if (formula->type == Formula::Xor)
        return "((" + formulaText(formula->left, names) + " v " +
               formulaText(formula->right, names) + ") & ~(" +
               formulaText(formula->left, names) + " & " +
               formulaText(formula->right, names) + "))";
    if (formula->type == Formula::Nor)
        return "~(" + formulaText(formula->left, names) + " v " +
               formulaText(formula->right, names) + ")";
    const std::string op = formula->type == Formula::And ? " & " :
                           formula->type == Formula::Or ? " v " :
                           formula->type == Formula::Xor ? " xor " :
                           formula->type == Formula::Iff ? " <-> " : " -> ";
    return "(" + formulaText(formula->left, names) + op +
           formulaText(formula->right, names) + ")";
}

bool evaluate(const Formula* formula, const std::map<std::string, bool>& values)
{
    if (formula->type == Formula::Atom) return values.at(formula->atom);
    if (formula->type == Formula::Not) return !evaluate(formula->left, values);
    if (formula->type == Formula::And)
        return evaluate(formula->left, values) && evaluate(formula->right, values);
    if (formula->type == Formula::Or)
        return evaluate(formula->left, values) || evaluate(formula->right, values);
    if (formula->type == Formula::Iff)
        return evaluate(formula->left, values) == evaluate(formula->right, values);
    if (formula->type == Formula::Xor)
        return evaluate(formula->left, values) != evaluate(formula->right, values);
    if (formula->type == Formula::Nor)
        return !evaluate(formula->left, values) && !evaluate(formula->right, values);
    return !evaluate(formula->left, values) || evaluate(formula->right, values);
}

std::vector<std::string> linesOf(const std::string& text)
{
    std::vector<std::string> result;
    std::stringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) if (!trim(line).empty()) result.push_back(trim(line));
    return result;
}

void drawText(sf::RenderWindow& window, sf::Font& font, const std::string& value,
              float x, float y, unsigned size, sf::Color color = sf::Color::White)
{
    sf::Text text(value, font, size);
    text.setFillColor(color);
    text.setPosition(x, y);
    window.draw(text);
}

std::vector<std::string> visualLines(const std::string& value, sf::Font& font,
                                     unsigned size, float maxWidth)
{
    std::vector<std::string> result;
    std::stringstream source(value);
    std::string logicalLine;
    while (std::getline(source, logicalLine))
    {
        if (logicalLine.empty())
        {
            result.push_back("");
            continue;
        }
        std::string current;
        std::stringstream words(logicalLine);
        std::string word;
        while (words >> word)
        {
            if (!current.empty())
            {
                sf::Text measured(current + " " + word, font, size);
                if (measured.getLocalBounds().width <= maxWidth)
                {
                    current += " " + word;
                    continue;
                }
                result.push_back(current);
                current.clear();
            }

            std::string remainder = word;
            while (!remainder.empty())
            {
                size_t fitting = remainder.size();
                while (fitting > 1)
                {
                    sf::Text measured(remainder.substr(0, fitting), font, size);
                    if (measured.getLocalBounds().width <= maxWidth) break;
                    --fitting;
                }
                current = remainder.substr(0, fitting);
                remainder.erase(0, fitting);
                if (!remainder.empty())
                {
                    result.push_back(current);
                    current.clear();
                }
            }
        }
        if (!current.empty()) result.push_back(current);
    }
    return result;
}

void drawPanel(sf::RenderWindow& window, float x, float y, float width, float height)
{
    sf::RectangleShape shape(sf::Vector2f(width, height));
    shape.setPosition(x, y);
    shape.setFillColor(Panel);
    shape.setOutlineThickness(1);
    shape.setOutlineColor(sf::Color(38, 57, 78));
    window.draw(shape);
}

void drawConnector(sf::RenderWindow& window, sf::Vector2f from, sf::Vector2f to,
                   sf::Color color = Muted)
{
    sf::Vertex connector[] = {sf::Vertex(from, color), sf::Vertex(to, color)};
    window.draw(connector, 2, sf::Lines);
}

std::string branchLiterals(const TableauBranch& branch,
                           const std::vector<std::string>& atoms)
{
    std::string result;
    for (const auto& literal : branch.literals)
    {
        const auto atom = std::find(atoms.begin(), atoms.end(), literal.first);
        const size_t atomIndex = atom == atoms.end()
            ? 0 : static_cast<size_t>(atom - atoms.begin());
        if (!result.empty()) result += ", ";
        result += literal.second ? "" : "~";
        result += static_cast<char>('p' + atomIndex);
    }
    if (result.empty()) result = "(sin literales)";
    if (result.size() > 25) result = result.substr(0, 22) + "...";
    return result;
}

bool button(sf::RenderWindow& window, sf::Font& font, const std::string& label,
            float x, float y, float width, bool hovered)
{
    sf::RectangleShape shape(sf::Vector2f(width, 34));
    shape.setPosition(x, y);
    shape.setFillColor(hovered ? PanelLight : Panel);
    shape.setOutlineThickness(1);
    shape.setOutlineColor(hovered ? Cyan : sf::Color(54, 75, 96));
    window.draw(shape);
    drawText(window, font, label, x + 12, y + 7, 14, hovered ? Cyan : sf::Color::White);
    return hovered;
}

std::string jsonEscape(const std::string& value)
{
    std::string output;
    for (char c : value)
    {
        if (c == '\\' || c == '"') output += '\\';
        output += c;
    }
    return output;
}

void appendUtf8(std::string& text, unsigned int codepoint)
{
    if (codepoint < 0x80) text += static_cast<char>(codepoint);
    else if (codepoint < 0x800)
    {
        text += static_cast<char>(0xC0 | (codepoint >> 6));
        text += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
    else
    {
        text += static_cast<char>(0xE0 | (codepoint >> 12));
        text += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        text += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
}

std::string clipboardText()
{
#ifdef _WIN32
    if (!OpenClipboard(nullptr)) return "";
    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (!handle)
    {
        CloseClipboard();
        return "";
    }
    const wchar_t* wideText = static_cast<const wchar_t*>(GlobalLock(handle));
    if (!wideText)
    {
        CloseClipboard();
        return "";
    }
    std::string result;
    for (size_t i = 0; wideText[i] != L'\0'; ++i)
    {
        unsigned int codepoint = static_cast<unsigned int>(wideText[i]);
        if (codepoint == '\r') continue;
        appendUtf8(result, codepoint);
    }
    GlobalUnlock(handle);
    CloseClipboard();
    return result;
#else
    return "";
#endif
}

void deleteFormula(Formula* formula)
{
    if (!formula) return;
    deleteFormula(formula->left);
    deleteFormula(formula->right);
    delete formula;
}

void addTableauLiteral(TableauBranch& branch, const std::string& atom, bool value)
{
    for (const auto& literal : branch.literals)
    {
        if (literal.first == atom && literal.second != value)
        {
            // Conserva el literal conflictivo para que la hoja explique el cierre.
            branch.literals.push_back({atom, value});
            branch.closed = true;
            return;
        }
    }
    branch.literals.push_back({atom, value});
}

void buildTableau(std::vector<std::pair<Formula*, bool>> pending,
                  TableauBranch branch, std::vector<TableauBranch>& result)
{
    if (pending.empty())
    {
        result.push_back(branch);
        return;
    }
    const Formula* formula = pending.back().first;
    const bool sign = pending.back().second;
    pending.pop_back();
    if (branch.closed)
    {
        result.push_back(branch);
        return;
    }
    if (formula->type == Formula::Atom)
    {
        addTableauLiteral(branch, formula->atom, sign);
        buildTableau(pending, branch, result);
        return;
    }
    if (formula->type == Formula::Not)
    {
        pending.push_back({formula->left, !sign});
        buildTableau(pending, branch, result);
        return;
    }
    const bool isAnd = formula->type == Formula::And;
    const bool isOr = formula->type == Formula::Or;
    const bool isXor = formula->type == Formula::Xor;
    const bool isNor = formula->type == Formula::Nor;
    const bool isImp = formula->type == Formula::Imp;
    const bool isIff = formula->type == Formula::Iff;
    if ((isAnd && sign) || (isOr && !sign))
    {
        pending.push_back({formula->right, sign});
        pending.push_back({formula->left, sign});
        buildTableau(pending, branch, result);
        return;
    }
    if ((isOr && sign) || (isAnd && !sign) || (isImp && sign))
    {
        TableauBranch leftBranch = branch;
        TableauBranch rightBranch = branch;
        if (isImp)
        {
            pending.push_back({formula->left, false});
            buildTableau(pending, leftBranch, result);
            pending.pop_back();
            pending.push_back({formula->right, true});
            buildTableau(pending, rightBranch, result);
        }
        else
        {
            pending.push_back({formula->left, sign});
            buildTableau(pending, leftBranch, result);
            pending.pop_back();
            pending.push_back({formula->right, sign});
            buildTableau(pending, rightBranch, result);
        }
        return;
    }
    if (isIff && sign)
    {
        TableauBranch sameBranch = branch;
        pending.push_back({formula->right, true});
        pending.push_back({formula->left, true});
        buildTableau(pending, sameBranch, result);

        TableauBranch oppositeBranch = branch;
        pending.pop_back();
        pending.pop_back();
        pending.push_back({formula->right, false});
        pending.push_back({formula->left, false});
        buildTableau(pending, oppositeBranch, result);
        return;
    }
    if (isXor && sign)
    {
        TableauBranch leftBranch = branch;
        TableauBranch rightBranch = branch;
        pending.push_back({formula->left, true});
        pending.push_back({formula->right, false});
        buildTableau(pending, leftBranch, result);
        pending.pop_back();
        pending.pop_back();
        pending.push_back({formula->left, false});
        pending.push_back({formula->right, true});
        buildTableau(pending, rightBranch, result);
        return;
    }
    if (isNor && sign)
    {
        pending.push_back({formula->right, false});
        pending.push_back({formula->left, false});
        buildTableau(pending, branch, result);
        return;
    }
    if (isNor && !sign)
    {
        TableauBranch leftBranch = branch;
        TableauBranch rightBranch = branch;
        pending.push_back({formula->left, true});
        buildTableau(pending, leftBranch, result);
        pending.pop_back();
        pending.push_back({formula->right, true});
        buildTableau(pending, rightBranch, result);
        return;
    }
    if (isXor && !sign)
    {
        TableauBranch sameBranch = branch;
        pending.push_back({formula->left, true});
        pending.push_back({formula->right, true});
        buildTableau(pending, sameBranch, result);
        pending.pop_back();
        pending.pop_back();
        TableauBranch oppositeBranch = branch;
        pending.push_back({formula->left, false});
        pending.push_back({formula->right, false});
        buildTableau(pending, oppositeBranch, result);
        return;
    }
    if (isIff && !sign)
    {
        TableauBranch leftBranch = branch;
        TableauBranch rightBranch = branch;
        pending.push_back({formula->left, true});
        pending.push_back({formula->right, false});
        buildTableau(pending, leftBranch, result);
        pending.pop_back();
        pending.pop_back();
        pending.push_back({formula->left, false});
        pending.push_back({formula->right, true});
        buildTableau(pending, rightBranch, result);
        return;
    }
    pending.push_back({formula->left, true});
    pending.push_back({formula->right, false});
    buildTableau(pending, branch, result);
}
}

int main(int argc, char** argv)
{
    sf::RenderWindow window(sf::VideoMode(1280, 760), "Inferencia Proposicional | Gatochi");
    window.setFramerateLimit(60);
    sf::Font font;
    if (!font.loadFromFile("C:/Windows/Fonts/consola.ttf") &&
        !font.loadFromFile("C:/Windows/Fonts/arial.ttf"))
        return 1;

    std::string input =
        "Si llueve, entonces el piso esta mojado.\n"
        "Llueve.";
    std::vector<Rule> rules;
    std::vector<std::string> atoms;
    std::vector<Model> models;
    std::vector<TableauBranch> tableau;
    bool satisfiable = false;
    bool analyzed = false;
    bool inputFocus = true;
    sf::Vector2i mouse;

    auto analyze = [&]()
    {
        for (Rule& rule : rules) deleteFormula(rule.formula);
        rules.clear();
        atoms.clear();
        models.clear();
        tableau.clear();
        std::map<std::string, bool> seen;
        std::map<std::string, char> names;
        const std::vector<std::string> source = linesOf(input);
        for (const std::string& original : source)
        {
            std::string text = lower(original);
            text = replaceAll(text, "á", "a");
            text = replaceAll(text, "é", "e");
            text = replaceAll(text, "í", "i");
            text = replaceAll(text, "ó", "o");
            text = replaceAll(text, "ú", "u");
            size_t ifPos = text.find("si ");
            size_t thenPos = text.find(" entonces ");
            size_t commaPos = text.find(',');
            size_t onlyPos = text.find(" solo ");
            size_t conditionPos = onlyPos == std::string::npos
                ? std::string::npos : text.find(" si ", onlyPos + 6);
            size_t iffPos = text.find(" si y solo si ");
            Formula* formula = nullptr;
            if (iffPos != std::string::npos)
            {
                Formula* left = parseClause(text.substr(0, iffPos));
                Formula* right = parseClause(text.substr(iffPos + 14));
                formula = make(Formula::Iff, left, right);
            }
            else if (ifPos == 0 && thenPos != std::string::npos)
            {
                Formula* premise = parseClause(text.substr(3, thenPos - 3));
                Formula* consequence = parseClause(text.substr(thenPos + 10));
                formula = make(Formula::Imp, premise, consequence);
            }
            else if (ifPos == 0 && commaPos != std::string::npos)
            {
                Formula* premise = parseClause(text.substr(3, commaPos - 3));
                Formula* consequence = parseClause(text.substr(commaPos + 1));
                formula = make(Formula::Imp, premise, consequence);
            }
            else if (onlyPos != std::string::npos && conditionPos != std::string::npos)
            {
                Formula* consequence = parseClause(text.substr(0, onlyPos));
                Formula* condition = parseClause(text.substr(conditionPos + 4));
                formula = make(Formula::Imp, consequence, condition);
            }
            else formula = parseClause(text);
            rules.push_back({original, formula, ""});
            std::vector<std::string> found;
            std::function<void(const Formula*)> collect = [&](const Formula* f)
            {
                if (!f) return;
                if (f->type == Formula::Atom) found.push_back(f->atom);
                else { collect(f->left); collect(f->right); }
            };
            collect(formula);
            for (const std::string& atom : found) if (!seen[atom])
            {
                seen[atom] = true;
                atoms.push_back(atom);
            }
        }
        for (size_t i = 0; i < atoms.size(); ++i)
            names[atoms[i]] = static_cast<char>('p' + i);
        for (Rule& rule : rules) rule.formal = formulaText(rule.formula, names);
        std::vector<std::pair<Formula*, bool>> pending;
        for (const Rule& rule : rules) pending.push_back({rule.formula, true});
        buildTableau(pending, TableauBranch{{}, false}, tableau);

        const size_t count = atoms.size() > 12 ? 12 : atoms.size();
        const unsigned long total = 1UL << count;
        for (unsigned long mask = 0; mask < total; ++mask)
        {
            Model model;
            for (size_t i = 0; i < count; ++i)
                model.values[atoms[i]] = (mask & (1UL << i)) != 0;
            bool valid = true;
            for (const Rule& rule : rules) if (!evaluate(rule.formula, model.values)) valid = false;
            if (valid) models.push_back(model);
        }
        satisfiable = !rules.empty() && !models.empty();
        analyzed = true;
    };

    if (argc > 1 && std::string(argv[1]) == "--test")
    {
        std::ifstream suite("test_suite.txt");
        if (!suite)
        {
            std::cerr << "[ ERROR ] No se encontro test_suite.txt\n";
            return 1;
        }
        std::string line;
        unsigned passed = 0;
        unsigned total = 0;
        while (std::getline(suite, line))
        {
            if (trim(line).empty() || trim(line)[0] == '#') continue;
            std::vector<std::string> fields;
            std::stringstream row(line);
            std::string field;
            while (std::getline(row, field, '|')) fields.push_back(trim(field));
            if (fields.size() != 4)
            {
                std::cout << "[ FAIL ] linea invalida: " << line << "\n";
                ++total;
                continue;
            }
            input.clear();
            std::stringstream rulesStream(fields[1]);
            while (std::getline(rulesStream, field, ';'))
            {
                if (!input.empty()) input += '\n';
                input += trim(field);
            }
            analyze();
            ++total;
            const bool expectedSat = fields[2] == "SATISFACIBLE";
            bool ok = satisfiable == expectedSat;
            std::string reason;
            if (!ok)
                reason = "esperaba " + fields[2] + " pero obtuvo " +
                         (satisfiable ? "SATISFACIBLE" : "INSATISFACIBLE");

            if (ok && expectedSat && fields[3] != "NINGUNO")
            {
                std::vector<std::string> expectedModels;
                std::stringstream modelStream(fields[3]);
                while (std::getline(modelStream, field, ';'))
                    expectedModels.push_back(trim(field));
                std::vector<std::string> actualModels;
                for (const Model& model : models)
                {
                    std::string value;
                    for (size_t i = 0; i < atoms.size(); ++i)
                    {
                        if (!value.empty()) value += ",";
                        value += static_cast<char>('p' + i);
                        value += model.values.at(atoms[i]) ? "=V" : "=F";
                    }
                    actualModels.push_back(value);
                }
                std::sort(expectedModels.begin(), expectedModels.end());
                std::sort(actualModels.begin(), actualModels.end());
                if (expectedModels != actualModels)
                {
                    ok = false;
                    reason = "modelos esperados no coinciden";
                }
            }
            if (ok)
            {
                ++passed;
                std::cout << "[ PASS ] " << fields[0] << "\n";
            }
            else
                std::cout << "[ FAIL ] " << fields[0] << " - Razon: " << reason << "\n";
        }
        const double accuracy = total == 0 ? 0.0 :
            (100.0 * static_cast<double>(passed) / static_cast<double>(total));
        std::cout << passed << "/" << total << " Pruebas pasadas - "
                  << accuracy << "% Accuracy\n";
        for (Rule& rule : rules) deleteFormula(rule.formula);
        return passed == total ? 0 : 1;
    }

    analyze();
    while (window.isOpen())
    {
        mouse = sf::Mouse::getPosition(window);
        sf::Event event;
        while (window.pollEvent(event))
        {
            if (event.type == sf::Event::Closed) window.close();
            if (event.type == sf::Event::MouseButtonPressed &&
                event.mouseButton.button == sf::Mouse::Left)
            {
                const float x = static_cast<float>(event.mouseButton.x);
                const float y = static_cast<float>(event.mouseButton.y);
                if (x >= 20 && x <= 380 && y >= 105 && y <= 520) inputFocus = true;
                else if (x >= 20 && x <= 190 && y >= 550 && y <= 590)
                {
                    analyze();
                    inputFocus = false;
                }
                else if (x >= 205 && x <= 375 && y >= 550 && y <= 590)
                {
                    std::ofstream file("analisis_proposicional.json");
                    file << "{\n  \"entrada\": \"" << jsonEscape(input) << "\",\n  \"atomos\": [";
                    for (size_t i = 0; i < atoms.size(); ++i)
                        file << (i ? ", " : "") << "\"" << jsonEscape(atoms[i]) << "\"";
                    file << "],\n  \"formulas\": [";
                    for (size_t i = 0; i < rules.size(); ++i)
                        file << (i ? ", " : "") << "\"" << jsonEscape(rules[i].formal) << "\"";
                    file << "],\n  \"resultado\": \"" << (satisfiable ? "Satisfacible" : "Insatisfacible") << "\"\n}\n";
                }
                else if (x >= 390 && x <= 560 && y >= 550 && y <= 590)
                {
                    input.clear();
                    for (Rule& rule : rules) deleteFormula(rule.formula);
                    rules.clear(); atoms.clear(); models.clear(); tableau.clear();
                    analyzed = false; satisfiable = false;
                    inputFocus = true;
                }
            }
            if (event.type == sf::Event::TextEntered && inputFocus)
            {
                if (event.text.unicode == 8)
                {
                    if (!input.empty()) input.pop_back();
                }
                else if (event.text.unicode >= 32 && event.text.unicode != 127)
                    appendUtf8(input, event.text.unicode);
            }
            if (event.type == sf::Event::KeyPressed && inputFocus &&
                event.key.code == sf::Keyboard::Return)
            {
                input += '\n';
            }
            if (event.type == sf::Event::KeyPressed && inputFocus &&
                event.key.code == sf::Keyboard::V &&
                (event.key.control || event.key.system))
            {
                input += clipboardText();
            }
        }

        window.clear(Background);
        drawText(window, font, "INFERENCE // PROPOSITIONAL LOGIC ENGINE", 20, 18, 22, Cyan);
        drawText(window, font, "5-STAGE PIPELINE  |  READY", 900, 23, 13, Green);
        drawPanel(window, 20, 65, 360, 465);
        drawPanel(window, 400, 65, 405, 465);
        drawPanel(window, 825, 65, 435, 465);
        drawText(window, font, "01  ENTRADA DE REGLAS", 35, 78, 16, Cyan);
        drawText(window, font, "Escribe una regla por linea:", 35, 102, 13, Muted);
        sf::RectangleShape editor(sf::Vector2f(330, 385));
        editor.setPosition(35, 125); editor.setFillColor(sf::Color(7, 11, 18));
        editor.setOutlineThickness(1); editor.setOutlineColor(inputFocus ? Cyan : sf::Color(54, 75, 96));
        window.draw(editor);
        std::vector<std::string> entered = visualLines(input, font, 13, 310);
        float textY = 138;
        for (const std::string& line : entered)
        {
            drawText(window, font, "> " + line, 45, textY, 13, sf::Color(220, 230, 240));
            textY += 23;
            if (textY > 488) break;
        }
        drawText(window, font, inputFocus ? "EDITANDO" : "PULSA ANALIZAR", 45, 490, 11, inputFocus ? Green : Muted);
        const bool hoverAnalyze = mouse.x >= 20 && mouse.x <= 190 && mouse.y >= 550 && mouse.y <= 590;
        const bool hoverSave = mouse.x >= 205 && mouse.x <= 375 && mouse.y >= 550 && mouse.y <= 590;
        const bool hoverClear = mouse.x >= 390 && mouse.x <= 560 && mouse.y >= 550 && mouse.y <= 590;
        button(window, font, "[ ANALIZAR ]", 20, 550, 170, hoverAnalyze);
        button(window, font, "[ GUARDAR JSON ]", 205, 550, 170, hoverSave);
        button(window, font, "[ CERRAR / LIMPIAR ]", 390, 550, 170, hoverClear);

        drawText(window, font, "02  EXTRACTOR DE ATOMOS", 415, 78, 16, Cyan);
        drawText(window, font, "Variable   Significado", 420, 108, 13, Muted);
        for (size_t i = 0; i < atoms.size() && i < 12; ++i)
        {
            std::string variable(1, static_cast<char>('p' + i));
            drawText(window, font, variable + "          " + atoms[i], 425, 132 + static_cast<float>(i) * 23, 13, Green);
        }
        drawText(window, font, "03  TRADUCTOR DE REGLAS", 415, 430, 16, Cyan);
        for (size_t i = 0; i < rules.size() && i < 3; ++i)
            drawText(window, font, "R" + std::to_string(i + 1) + "  " + rules[i].formal, 425, 458 + static_cast<float>(i) * 20, 13);

        drawText(window, font, "04  ARBOL SEMANTICO / TABLEAU", 840, 78, 16, Cyan);
        drawText(window, font, "Enramajes y hojas del arbol:", 850, 105, 12, Muted);
        sf::RectangleShape root(sf::Vector2f(210, 28));
        root.setPosition(937, 125);
        root.setFillColor(PanelLight);
        root.setOutlineThickness(1);
        root.setOutlineColor(Cyan);
        window.draw(root);
        drawText(window, font, analyzed ? "REGLAS / RAIZ" : "SIN ANALISIS", 963, 131, 12);

        const size_t visibleBranches = std::min<size_t>(tableau.size(), 6);
        const float firstLeafX = 850.0f;
        const float leafY = 250.0f;
        const float rootX = 1042.0f;
        const float rootY = 153.0f;
        if (visibleBranches == 0)
        {
            drawText(window, font, "Sin ramas: entrada vacia", 930, 205, 12, Muted);
        }
        else
        {
            for (size_t branchIndex = 0; branchIndex < visibleBranches; ++branchIndex)
            {
                const TableauBranch& branch = tableau[branchIndex];
                const float x = visibleBranches == 1
                    ? 1028.0f : firstLeafX + static_cast<float>(branchIndex) *
                      ((1230.0f - firstLeafX) / static_cast<float>(visibleBranches - 1));
                const float splitX = visibleBranches == 1 ? rootX : (rootX + x) / 2.0f;
                drawConnector(window, sf::Vector2f(rootX, rootY),
                              sf::Vector2f(splitX, 195.0f), branch.closed ? Red : Green);
                drawConnector(window, sf::Vector2f(splitX, 195.0f),
                              sf::Vector2f(x, leafY), branch.closed ? Red : Green);

                sf::CircleShape splitNode(4);
                splitNode.setOrigin(4, 4);
                splitNode.setPosition(splitX, 195);
                splitNode.setFillColor(branch.closed ? Red : Green);
                window.draw(splitNode);

                sf::RectangleShape leaf(sf::Vector2f(72, 54));
                leaf.setPosition(x - 36, leafY);
                leaf.setFillColor(branch.closed ? sf::Color(62, 28, 38) : sf::Color(24, 58, 49));
                leaf.setOutlineThickness(1);
                leaf.setOutlineColor(branch.closed ? Red : Green);
                window.draw(leaf);
                drawText(window, font, "B" + std::to_string(branchIndex + 1),
                         x - 30, leafY + 5, 10, branch.closed ? Red : Green);
                drawText(window, font, branch.closed ? "[X]" : "[ ]",
                         x - 30, leafY + 20, 10, branch.closed ? Red : Green);
                std::string literals = branchLiterals(branch, atoms);
                if (literals.size() > 12) literals = literals.substr(0, 9) + "...";
                sf::Text literalText(literals, font, 9);
                literalText.setFillColor(sf::Color::White);
                const float literalWidth = literalText.getLocalBounds().width;
                literalText.setPosition(x - literalWidth / 2.0f, leafY + 37);
                window.draw(literalText);
            }
        }
        drawText(window, font, "Verde = abierta    Rojo/X = cerrada", 850, 320, 11, Muted);
        drawText(window, font, "05  REPORTE DE MODELOS", 840, 350, 16, Cyan);
        drawText(window, font, "ESTADO:", 850, 385, 13, Muted);
        drawText(window, font, satisfiable ? "SATISFACIBLE" : "INSATISFACIBLE", 930, 383, 17, satisfiable ? Green : Red);
        drawText(window, font, "Modelos que cumplen todas las reglas:", 850, 415, 12, Muted);
        for (size_t i = 0; i < models.size() && i < 8; ++i)
        {
            std::string row = "M" + std::to_string(i + 1) + "  ";
            for (size_t atomIndex = 0; atomIndex < atoms.size(); ++atomIndex)
                row += std::string(1, static_cast<char>('p' + atomIndex)) + "=" +
                       (models[i].values.at(atoms[atomIndex]) ? "V  " : "F  ");
            drawText(window, font, row, 855, 440 + static_cast<float>(i) * 18, 12, Green);
        }
        drawText(window, font, "Guardar crea: analisis_proposicional.json", 840, 505, 11, Muted);
        window.display();
    }
    for (Rule& rule : rules) deleteFormula(rule.formula);
    return 0;
}
