#include <SFML/Graphics.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <ctime>
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

struct SavedMemory
{
    std::string name;
    std::string input;
};

struct VisualLine
{
    std::string text;
    size_t start;
    size_t end;
    bool logicalBreak;
};

class PremiseEditor
{
public:
    size_t cursor = 0;
    bool focused = true;

    void insert(std::string& value, const std::string& text)
    {
        value.insert(cursor, text);
        cursor += text.size();
    }

    void eraseBackward(std::string& value)
    {
        if (cursor == 0) return;
        size_t begin = cursor - 1;
        while (begin > 0 && (static_cast<unsigned char>(value[begin]) & 0xC0) == 0x80)
            --begin;
        value.erase(begin, cursor - begin);
        cursor = begin;
    }

    void moveLeft(const std::string& value)
    {
        if (cursor == 0) return;
        --cursor;
        while (cursor > 0 && (static_cast<unsigned char>(value[cursor]) & 0xC0) == 0x80)
            --cursor;
    }

    void moveRight(const std::string& value)
    {
        if (cursor >= value.size()) return;
        ++cursor;
        while (cursor < value.size() && (static_cast<unsigned char>(value[cursor]) & 0xC0) == 0x80)
            ++cursor;
    }

    size_t cursorFromPixel(const std::string& value, sf::Font& font, unsigned size,
                           float x, float y, float maxWidth) const;
};

class VerticalScrollbar
{
public:
    float offset = 0.0f;
    bool dragging = false;
    float dragGrab = 0.0f;

    float maxOffset(size_t itemCount, float visibleHeight, float rowHeight) const
    {
        return std::max(0.0f, itemCount * rowHeight - visibleHeight);
    }

    void scroll(float delta, size_t itemCount, float visibleHeight, float rowHeight)
    {
        offset -= delta * rowHeight;
        clamp(itemCount, visibleHeight, rowHeight);
    }

    void clamp(size_t itemCount, float visibleHeight, float rowHeight)
    {
        offset = std::max(0.0f, std::min(offset, maxOffset(itemCount, visibleHeight, rowHeight)));
    }

    bool hit(float x, float y, float left, float top, float width, float height) const
    {
        return x >= left && x <= left + width && y >= top && y <= top + height;
    }

    void beginDrag(float y, float top, float height, size_t itemCount,
                   float visibleHeight, float rowHeight)
    {
        const float max = maxOffset(itemCount, visibleHeight, rowHeight);
        const float thumbHeight = max == 0.0f
            ? height : std::max(18.0f, height * visibleHeight / (itemCount * rowHeight));
        const float travel = height - thumbHeight;
        const float thumbTop = top + (max == 0.0f ? 0.0f : travel * offset / max);
        dragGrab = y - thumbTop;
        dragging = true;
    }

    void dragTo(float y, float top, float height, size_t itemCount,
                float visibleHeight, float rowHeight)
    {
        const float max = maxOffset(itemCount, visibleHeight, rowHeight);
        const float thumbHeight = max == 0.0f
            ? height : std::max(18.0f, height * visibleHeight / (itemCount * rowHeight));
        const float travel = std::max(1.0f, height - thumbHeight);
        offset = ((y - dragGrab - top) / travel) * max;
        clamp(itemCount, visibleHeight, rowHeight);
    }

    void endDrag() { dragging = false; }
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

bool saveMemories(const std::vector<SavedMemory>& memories,
                  const std::string& path = "memorias_guardadas.db")
{
    std::ofstream file(path.c_str(), std::ios::trunc);
    if (!file) return false;
    auto encode = [](const std::string& value)
    {
        std::string encoded;
        for (char c : value)
        {
            if (c == '%') encoded += "%25";
            else if (c == '|') encoded += "%7C";
            else if (c == '\n') encoded += "%0A";
            else if (c == '\r') encoded += "%0D";
            else encoded += c;
        }
        return encoded;
    };
    for (const SavedMemory& memory : memories)
        file << encode(memory.name) << "|" << encode(memory.input) << "\n";
    return file.good();
}

std::vector<SavedMemory> loadMemories(const std::string& path = "memorias_guardadas.db")
{
    std::ifstream file(path.c_str());
    std::vector<SavedMemory> memories;
    if (!file) return memories;
    auto decode = [](const std::string& value)
    {
        std::string decoded;
        for (size_t i = 0; i < value.size(); ++i)
        {
            if (value[i] == '%' && i + 2 < value.size())
            {
                const std::string code = value.substr(i + 1, 2);
                if (code == "25") { decoded += '%'; i += 2; continue; }
                if (code == "7C") { decoded += '|'; i += 2; continue; }
                if (code == "0A") { decoded += '\n'; i += 2; continue; }
                if (code == "0D") { decoded += '\r'; i += 2; continue; }
            }
            decoded += value[i];
        }
        return decoded;
    };
    std::string line;
    while (std::getline(file, line))
    {
        if (trim(line).empty()) continue;
        const size_t sep = line.find('|');
        if (sep == std::string::npos) continue;
        SavedMemory memory;
        memory.name = decode(line.substr(0, sep));
        memory.input = decode(line.substr(sep + 1));
        memories.push_back(memory);
    }
    return memories;
}

std::string memoryTimestamp()
{
    std::time_t now = std::time(nullptr);
    std::tm localTime;
#ifdef _WIN32
    localtime_s(&localTime, &now);
#else
    localTime = *std::localtime(&now);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime);
    return std::string(buffer);
}

bool startsWith(const std::string& text, const std::string& prefix)
{
    return text.compare(0, prefix.size(), prefix) == 0;
}

std::string replaceBounded(std::string text, const std::string& from,
                           const std::string& to)
{
    size_t position = 0;
    while ((position = text.find(from, position)) != std::string::npos)
    {
        const bool atStart = position == 0 ||
            !std::isalnum(static_cast<unsigned char>(text[position - 1]));
        const size_t after = position + from.size();
        const bool atEnd = after == text.size() ||
            !std::isalnum(static_cast<unsigned char>(text[after]));
        if (atStart && atEnd)
        {
            text.replace(position, from.size(), to);
            position += to.size();
        }
        else
            position += from.size();
    }
    return text;
}

std::string normalizeVerbConjugations(std::string text)
{
    const std::vector<std::pair<std::string, std::string>> forms = {
        {"esta lloviendo", "llover"}, {"estaba lloviendo", "llover"},
        {"ha llovido", "llover"}, {"habia llovido", "llover"},
        {"llueve", "llover"}, {"llovio", "llover"},
        {"llovia", "llover"}, {"llovera", "llover"}, {"lloveria", "llover"},
        {"esta arrancando", "arrancar"}, {"arranca", "arrancar"},
        {"arranco", "arrancar"}, {"arrancara", "arrancar"},
        {"esta moviendose", "mover"}, {"esta moviendo", "mover"},
        {"se mueve", "mover"}, {"mueve", "mover"}, {"movio", "mover"},
        {"esta sonando", "sonar"}, {"suena", "sonar"}, {"sono", "sonar"},
        {"se enciende", "encender"}, {"esta encendida", "encender"},
        {"enciende", "encender"}, {"encendio", "encender"},
        {"detecta", "detectar"}, {"esta detectando", "detectar"},
        {"reinicia", "reiniciar"}, {"esta reiniciando", "reiniciar"},
        {"responde", "responder"}, {"esta respondiendo", "responder"},
        {"abre", "abrir"}, {"esta abriendo", "abrir"},
        {"activa", "activar"}, {"esta activando", "activar"},
        {"habilita", "habilitar"}, {"esta habilitando", "habilitar"},
        {"funciona", "funcionar"}, {"esta funcionando", "funcionar"},
        {"funciono", "funcionar"}, {"funcionara", "funcionar"},
        {"enciende", "encender"}, {"encendio", "encender"},
        {"apaga", "apagar"}, {"esta apagando", "apagar"}, {"apago", "apagar"},
        {"llega", "llegar"}, {"esta llegando", "llegar"}, {"llego", "llegar"},
        {"sale", "salir"}, {"esta saliendo", "salir"}, {"salio", "salir"},
        {"entra", "entrar"}, {"esta entrando", "entrar"}, {"entro", "entrar"},
        {"inicia", "iniciar"}, {"esta iniciando", "iniciar"}, {"inicio", "iniciar"},
        {"termina", "terminar"}, {"esta terminando", "terminar"},
        {"termino", "terminar"}, {"cambia", "cambiar"}, {"esta cambiando", "cambiar"},
        {"cambio", "cambiar"}, {"crea", "crear"}, {"esta creando", "crear"},
        {"creo", "crear"}, {"usa", "usar"}, {"esta usando", "usar"},
        {"uso", "usar"}, {"muestra", "mostrar"}, {"esta mostrando", "mostrar"},
        {"mostro", "mostrar"}, {"guarda", "guardar"}, {"esta guardando", "guardar"},
        {"guardo", "guardar"}, {"borra", "borrar"}, {"esta borrando", "borrar"},
        {"borro", "borrar"}, {"lee", "leer"}, {"esta leyendo", "leer"},
        {"leyo", "leer"}, {"escribe", "escribir"}, {"esta escribiendo", "escribir"},
        {"escribio", "escribir"}, {"dice", "decir"}, {"esta diciendo", "decir"},
        {"dijo", "decir"}, {"hace", "hacer"}, {"esta haciendo", "hacer"},
        {"hizo", "hacer"}, {"puede", "poder"}, {"esta pudiendo", "poder"},
        {"pudo", "poder"}, {"debe", "deber"}, {"esta debiendo", "deber"},
        {"debio", "deber"}, {"quiere", "querer"}, {"esta queriendo", "querer"},
        {"quiso", "querer"}, {"sabe", "saber"}, {"esta sabiendo", "saber"},
        {"supo", "saber"}, {"ve", "ver"}, {"esta viendo", "ver"},
        {"vio", "ver"}, {"da", "dar"}, {"esta dando", "dar"}, {"dio", "dar"},
        {"tiene", "tener"}, {"esta teniendo", "tener"}, {"tuvo", "tener"},
        {"viene", "venir"}, {"esta viniendo", "venir"}, {"vino", "venir"},
        {"va", "ir"}, {"esta yendo", "ir"}, {"fue", "ir"},
        {"esta siendo", "ser"},
        {"comiendo", "comer"}, {"partiendo", "partir"},
        {"viviendo", "vivir"}, {"leyendo", "leer"},
        {"durmiendo", "dormir"}, {"diciendo", "decir"},
        {"haciendo", "hacer"}, {"pudiendo", "poder"},
        {"teniendo", "tener"}, {"viniendo", "venir"}, {"yendo", "ir"}
    };
    for (const auto& form : forms)
        text = replaceBounded(text, form.first, form.second);

    std::stringstream words(text);
    std::string word;
    std::string normalized;
    while (words >> word)
    {
        if (!normalized.empty()) normalized += ' ';
        if (word.size() > 5 && word.compare(word.size() - 5, 5, "ando") == 0)
            word = word.substr(0, word.size() - 4) + "ar";
        normalized += word;
    }
    if (!normalized.empty()) text = normalized;
    return text;
}

size_t findFirstConnector(const std::string& text,
                          const std::vector<std::string>& connectors,
                          std::string& connector)
{
    size_t position = std::string::npos;
    for (const std::string& candidate : connectors)
    {
        const size_t found = text.find(candidate);
        if (found != std::string::npos && (position == std::string::npos || found < position))
        {
            position = found;
            connector = candidate;
        }
    }
    return position;
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
    text = normalizeVerbConjugations(text);
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
    text = replaceAll(text, "o bien ", "o ");
    text = replaceAll(text, "ya sea ", "");
    text = replaceAll(text, "tanto ", "");
    text = replaceAll(text, " como ", " y ");
    text = replaceAll(text, " ademas ", " y ");
    text = replaceAll(text, " ademas de ", " y ");
    text = replaceAll(text, " tambien ", " y ");
    text = replaceAll(text, " aunque ", " y ");
    text = replaceAll(text, " sin embargo ", " y ");
    text = replaceAll(text, " asi como ", " y ");
    text = replaceAll(text, " junto con ", " y ");
    text = replaceAll(text, " al igual que ", " y ");
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
    const std::vector<std::string> negationPrefixes = {
        "no es cierto que ", "es falso que ", "es mentira que ",
        "es incorrecto que ", "no ocurre que "
    };
    for (const std::string& negationPrefix : negationPrefixes)
    {
        if (!startsWith(text, negationPrefix)) continue;
        std::string negated = trim(text.substr(negationPrefix.size()));
        const std::vector<std::string> conjunctionSuffixes = {
            " al mismo tiempo", " simultaneamente", " a la vez"
        };
        for (const std::string& suffix : conjunctionSuffixes)
        {
            if (negated.size() > suffix.size() &&
                negated.compare(negated.size() - suffix.size(), suffix.size(), suffix) == 0)
            {
                negated = trim(negated.substr(0, negated.size() - suffix.size()));
                break;
            }
        }
        std::string connector;
        const size_t split = findFirstConnector(negated, {" y ", " o "}, connector);
        if (split != std::string::npos)
        {
            Formula* left = parseClause(negated.substr(0, split));
            Formula* right = parseClause(negated.substr(split + connector.size()));
            return make(Formula::Not, make(connector == " y " ? Formula::And : Formula::Or,
                                           left, right));
        }
        return make(Formula::Not, atomFormula(negated));
    }
    if (startsWith(text, "nunca ") || startsWith(text, "jamas "))
        return make(Formula::Not, atomFormula(text.substr(6)));
    if (startsWith(text, "ni ") && text.find(" ni ", 3) != std::string::npos)
    {
        const size_t split = text.find(" ni ", 3);
        return make(Formula::Nor, parseClause(text.substr(3, split - 3)),
                    parseClause(text.substr(split + 4)));
    }
    if (text.compare(0, 3, "no ") == 0)
    {
        std::string rest = trim(text.substr(3));
        size_t andPos = rest.find(" y ");
        if (andPos != std::string::npos)
            return make(Formula::Not, make(Formula::And,
                parseClause(rest.substr(0, andPos)),
                parseClause(rest.substr(andPos + 3))));
        size_t orPos = rest.find(" o ");
        if (orPos != std::string::npos)
            return make(Formula::Not, make(Formula::Or,
                parseClause(rest.substr(0, orPos)),
                parseClause(rest.substr(orPos + 3))));
        return make(Formula::Not, atomFormula(rest));
    }
    size_t internalNot = text.find(" no ");
    if (internalNot != std::string::npos &&
        text.find(" y ") == std::string::npos &&
        text.find(" o ") == std::string::npos)
    {
        std::string positive = trim(text.substr(0, internalNot) + " " +
                                    text.substr(internalNot + 4));
        return make(Formula::Not, atomFormula(positive));
    }

    const std::string exclusiveSuffix = " pero no ambos";
    size_t exclusivePos = text.find(exclusiveSuffix);
    size_t andPos = text.find(" y ");
    if (andPos != std::string::npos)
        return make(Formula::And, parseClause(text.substr(0, andPos)),
                    parseClause(text.substr(andPos + 3)));
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

std::vector<VisualLine> visualLineRanges(const std::string& value, sf::Font& font,
                                         unsigned size, float maxWidth)
{
    std::vector<VisualLine> result;
    size_t logicalStart = 0;
    while (logicalStart <= value.size())
    {
        const size_t newline = value.find('\n', logicalStart);
        const size_t logicalEnd = newline == std::string::npos ? value.size() : newline;
        size_t start = logicalStart;
        if (start == logicalEnd)
            result.push_back({"", start, start, true});
        while (start < logicalEnd)
        {
            size_t end = start;
            while (end < logicalEnd)
            {
                const size_t next = end + 1;
                sf::Text measured(value.substr(start, next - start), font, size);
                if (measured.getLocalBounds().width > maxWidth && end > start) break;
                end = next;
            }
            result.push_back({value.substr(start, end - start), start, end, end == logicalEnd});
            start = end;
        }
        if (newline == std::string::npos) break;
        logicalStart = newline + 1;
    }
    return result;
}

size_t PremiseEditor::cursorFromPixel(const std::string& value, sf::Font& font,
                                      unsigned size, float x, float y,
                                      float maxWidth) const
{
    const std::vector<VisualLine> lines = visualLineRanges(value, font, size, maxWidth);
    if (lines.empty()) return 0;
    const size_t row = static_cast<size_t>(std::max(0.0f, std::floor((y - 138.0f) / 23.0f)));
    const VisualLine& line = lines[std::min(row, lines.size() - 1)];
    size_t best = line.start;
    float bestDistance = std::numeric_limits<float>::max();
    for (size_t position = line.start; position <= line.end; ++position)
    {
        sf::Text measured(value.substr(line.start, position - line.start), font, size);
        const float candidate = 45.0f + measured.getLocalBounds().width;
        const float distance = std::abs(x - candidate);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = position;
        }
    }
    while (best < value.size() && (static_cast<unsigned char>(value[best]) & 0xC0) == 0x80)
        ++best;
    return best;
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
    return result;
}

std::string literalDescription(const std::string& atom, bool value)
{
    return value ? atom : "no " + atom;
}

bool formulaContainsAtom(const Formula* formula, const std::string& atom)
{
    if (!formula) return false;
    if (formula->type == Formula::Atom) return formula->atom == atom;
    return formulaContainsAtom(formula->left, atom) ||
           formulaContainsAtom(formula->right, atom);
}

std::string tooltipLines(const std::string& text, size_t width)
{
    std::stringstream words(text);
    std::string word;
    std::string line;
    std::string result;
    while (words >> word)
    {
        if (!line.empty() && line.size() + word.size() + 1 > width)
        {
            if (!result.empty()) result += '\n';
            result += line;
            line.clear();
        }
        if (!line.empty()) line += ' ';
        line += word;
    }
    if (!line.empty())
    {
        if (!result.empty()) result += '\n';
        result += line;
    }
    return result;
}

std::string branchReason(const TableauBranch& branch,
                         const std::vector<Rule>& rules)
{
    for (size_t i = 0; i < branch.literals.size(); ++i)
        for (size_t j = i + 1; j < branch.literals.size(); ++j)
            if (branch.literals[i].first == branch.literals[j].first &&
                branch.literals[i].second != branch.literals[j].second)
            {
                const std::string& atomName = branch.literals[i].first;
                const bool firstValue = branch.literals[i].second;
                const std::string fact = literalDescription(atomName, firstValue);
                const std::string opposite = literalDescription(atomName, !firstValue);
                std::string explanation =
                    "Contradiccion: la rama exige \"" + fact + "\" y \"" +
                    opposite + "\" al mismo tiempo. No pueden ser verdaderas " +
                    "ambas cosas a la vez.";
                for (const Rule& rule : rules)
                {
                    if (formulaContainsAtom(rule.formula, atomName))
                    {
                        explanation += " Entra en conflicto con: " + rule.original;
                        break;
                    }
                }
                explanation += " La rama queda cerrada.";
                return tooltipLines(explanation, 58);
            }
    if (branch.closed)
        return "Rama cerrada: una de sus reglas obliga a dos valores incompatibles. "
               "Por eso este camino no puede satisfacer todas las premisas.";

    std::string explanation = "Rama abierta y satisfacible: ";
    if (branch.literals.empty())
        explanation += "no impone hechos incompatibles.";
    else
    {
        explanation += "pueden cumplirse a la vez ";
        for (size_t i = 0; i < branch.literals.size(); ++i)
        {
            if (i > 0) explanation += (i + 1 == branch.literals.size() ? " y " : ", ");
            explanation += "\"" + literalDescription(branch.literals[i].first,
                                                     branch.literals[i].second) + "\"";
        }
        explanation += ". Ningun hecho de esta rama contradice a otro.";
    }
    return tooltipLines(explanation, 58);
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
    std::vector<SavedMemory> memories = loadMemories();
    int selectedMemory = -1;
    size_t memoryOffset = 0;
    std::string memoryMessage = "Memoria lista.";
    bool satisfiable = false;
    bool analyzed = false;
    bool inputFocus = true;
    sf::Vector2i mouse;
    PremiseEditor editor;
    VerticalScrollbar ruleScroll;
    bool started = false;
    bool treePanning = false;
    sf::Vector2i treePanAnchor;
    std::string tooltip;
    sf::Clock caretClock;

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
            std::string iffConnector;
            size_t iffPos = findFirstConnector(text,
                {" si y solo si ", " si y solamente si ", " si y unicamente si ",
                 " cuando y solo cuando ", " equivale a ", " es equivalente a "},
                iffConnector);
            Formula* formula = nullptr;
            if (iffPos != std::string::npos)
            {
                Formula* left = parseClause(text.substr(0, iffPos));
                Formula* right = parseClause(text.substr(iffPos + iffConnector.size()));
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
            else
            {
                std::string conditionalPrefix;
                const std::vector<std::string> conditionalPrefixes = {
                    "en caso de que ", "siempre que ", "a condicion de que ",
                    "con tal de que ", "dado que ", "cuando "
                };
                for (const std::string& candidate : conditionalPrefixes)
                {
                    if (text.compare(0, candidate.size(), candidate) == 0)
                    {
                        conditionalPrefix = candidate;
                        break;
                    }
                }
                if (!conditionalPrefix.empty() && commaPos != std::string::npos)
                {
                    Formula* premise = parseClause(text.substr(
                        conditionalPrefix.size(), commaPos - conditionalPrefix.size()));
                    Formula* consequence = parseClause(text.substr(commaPos + 1));
                    formula = make(Formula::Imp, premise, consequence);
                }
                else
                    formula = nullptr;
            }
            if (!formula && onlyPos != std::string::npos && conditionPos != std::string::npos)
            {
                Formula* consequence = parseClause(text.substr(0, onlyPos));
                Formula* condition = parseClause(text.substr(conditionPos + 4));
                formula = make(Formula::Imp, consequence, condition);
            }
            if (!formula)
            {
                std::string implicationConnector;
                const size_t implicationPos = findFirstConnector(text,
                    {" es condicion suficiente para ", " es condicion necesaria para ",
                     " es condicion necesaria y suficiente para ", " basta para ",
                     " implica ", " unicamente si ", " solo cuando ", " solo si "},
                    implicationConnector);
                if (implicationPos != std::string::npos)
                {
                    Formula* left = parseClause(text.substr(0, implicationPos));
                    Formula* right = parseClause(
                        text.substr(implicationPos + implicationConnector.size()));
                    if (implicationConnector == " es condicion necesaria para ")
                        formula = make(Formula::Imp, right, left);
                    else if (implicationConnector == " es condicion necesaria y suficiente para ")
                        formula = make(Formula::Iff, left, right);
                    else
                        formula = make(Formula::Imp, left, right);
                }
                else
                    formula = parseClause(text);
            }
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

        std::function<void(size_t, std::map<std::string, bool>&)> enumerateModels =
            [&](size_t index, std::map<std::string, bool>& assignment)
            {
                if (index == atoms.size())
                {
                    bool valid = true;
                    for (const Rule& rule : rules)
                    {
                        if (!evaluate(rule.formula, assignment))
                        {
                            valid = false;
                            break;
                        }
                    }
                    if (valid) models.push_back({assignment});
                    return;
                }
                assignment[atoms[index]] = false;
                enumerateModels(index + 1, assignment);
                assignment[atoms[index]] = true;
                enumerateModels(index + 1, assignment);
            };
        std::map<std::string, bool> current;
        enumerateModels(0, current);
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
    sf::View treeView(sf::FloatRect(825.0f, 65.0f, 435.0f, 465.0f));
    treeView.setCenter(1042.0f, 290.0f);
    treeView.setViewport(sf::FloatRect(825.0f / 1280.0f, 65.0f / 760.0f,
                                       435.0f / 1280.0f, 465.0f / 760.0f));
    while (window.isOpen())
    {
        mouse = sf::Mouse::getPosition(window);
        sf::Event event;
        while (window.pollEvent(event))
        {
            if (event.type == sf::Event::Closed) window.close();
            if (event.type == sf::Event::MouseButtonPressed &&
                event.mouseButton.button == sf::Mouse::Left && !started)
            {
                started = true;
                inputFocus = true;
                editor.focused = true;
                continue;
            }
            if (event.type == sf::Event::KeyPressed &&
                event.key.code == sf::Keyboard::Return && !started)
            {
                started = true;
                inputFocus = true;
                editor.focused = true;
                continue;
            }
            if (event.type == sf::Event::MouseButtonPressed &&
                event.mouseButton.button == sf::Mouse::Left)
            {
                const float x = static_cast<float>(event.mouseButton.x);
                const float y = static_cast<float>(event.mouseButton.y);
                if (x >= 825 && x <= 1260 && y >= 65 && y <= 530)
                {
                    const sf::Vector2f world = window.mapPixelToCoords(
                        sf::Vector2i(event.mouseButton.x, event.mouseButton.y), treeView);
                    const size_t visibleBranches = tableau.size();
                    for (size_t branchIndex = 0; branchIndex < visibleBranches; ++branchIndex)
                    {
                        const float branchX = visibleBranches == 1
                            ? 1042.0f : 700.0f + static_cast<float>(branchIndex) * 125.0f;
                        const sf::FloatRect hit(branchX - 55.0f, 300.0f, 110.0f, 62.0f);
                        if (hit.contains(world))
                        {
                            tooltip = "B" + std::to_string(branchIndex + 1) + ": " +
                                      branchReason(tableau[branchIndex], rules);
                            break;
                        }
                    }
                    treePanning = true;
                    treePanAnchor = sf::Vector2i(event.mouseButton.x, event.mouseButton.y);
                }
                else if (x >= 35 && x <= 365 && y >= 125 && y <= 510)
                {
                    inputFocus = true;
                    editor.focused = true;
                    editor.cursor = editor.cursorFromPixel(input, font, 13,
                        x, y, 310.0f);
                }
                else if (ruleScroll.hit(x, y, 780, 445, 15, 70))
                    ruleScroll.beginDrag(y, 445, 70, rules.size(), 70, 20);
                else if (x >= 20 && x <= 190 && y >= 550 && y <= 590)
                {
                    analyze();
                    editor.cursor = input.size();
                    inputFocus = false;
                    editor.focused = false;
                    memoryMessage = "Analisis actualizado.";
                }
                else if (x >= 205 && x <= 375 && y >= 550 && y <= 590)
                {
                    if (trim(input).empty())
                    {
                        memoryMessage = "No hay entrada para guardar.";
                    }
                    else
                    {
                        SavedMemory memory;
                        memory.name = "Memoria " + memoryTimestamp();
                        memory.input = input;
                        memories.push_back(memory);
                        if (saveMemories(memories))
                        {
                            selectedMemory = static_cast<int>(memories.size()) - 1;
                            memoryOffset = memories.size() > 4 ? memories.size() - 4 : 0;
                            memoryMessage = "Memoria guardada correctamente.";
                        }
                        else
                            memoryMessage = "Error al guardar memorias.";
                    }
                }
                else if (x >= 390 && x <= 560 && y >= 550 && y <= 590)
                {
                    if (selectedMemory >= 0 &&
                        selectedMemory < static_cast<int>(memories.size()))
                    {
                        input = memories[static_cast<size_t>(selectedMemory)].input;
                        editor.cursor = input.size();
                        analyze();
                        inputFocus = false;
                        memoryMessage = "Memoria cargada y analizada.";
                    }
                    else
                        memoryMessage = "Selecciona una memoria para cargar.";
                }
                else if (x >= 20 && x <= 190 && y >= 595 && y <= 635)
                {
                    if (selectedMemory >= 0 &&
                        selectedMemory < static_cast<int>(memories.size()))
                    {
                        memories.erase(memories.begin() + selectedMemory);
                        if (selectedMemory >= static_cast<int>(memories.size()))
                            selectedMemory = static_cast<int>(memories.size()) - 1;
                        if (memoryOffset > 0 && memoryOffset >= memories.size())
                            memoryOffset = memories.size() > 4 ? memories.size() - 4 : 0;
                        if (saveMemories(memories))
                            memoryMessage = "Memoria borrada.";
                        else
                            memoryMessage = "Error al guardar el borrado.";
                    }
                    else
                        memoryMessage = "Selecciona una memoria para borrar.";
                }
                else if (x >= 205 && x <= 375 && y >= 595 && y <= 635)
                {
                    std::ofstream file("analisis_proposicional.json");
                    file << "{\n  \"entrada\": \"" << jsonEscape(input) << "\",\n  \"atomos\": [";
                    for (size_t i = 0; i < atoms.size(); ++i)
                        file << (i ? ", " : "") << "\"" << jsonEscape(atoms[i]) << "\"";
                    file << "],\n  \"formulas\": [";
                    for (size_t i = 0; i < rules.size(); ++i)
                        file << (i ? ", " : "") << "\"" << jsonEscape(rules[i].formal) << "\"";
                    file << "],\n  \"resultado\": \"" << (satisfiable ? "Satisfacible" : "Insatisfacible") << "\"\n}\n";
                    memoryMessage = file ? "JSON exportado." : "Error al exportar JSON.";
                }
                else if (x >= 390 && x <= 560 && y >= 595 && y <= 635)
                {
                    input.clear();
                    editor.cursor = 0;
                    for (Rule& rule : rules) deleteFormula(rule.formula);
                    rules.clear(); atoms.clear(); models.clear(); tableau.clear();
                    analyzed = false; satisfiable = false;
                    inputFocus = true;
                    memoryMessage = "Entrada limpiada.";
                }
                else if (x >= 760 && x <= 790 && y >= 665 && y <= 695)
                {
                    if (memoryOffset > 0) --memoryOffset;
                }
                else if (x >= 760 && x <= 790 && y >= 700 && y <= 730)
                {
                    if (memoryOffset + 4 < memories.size()) ++memoryOffset;
                }
                else if (x >= 30 && x <= 740 && y >= 665 && y <= 739)
                {
                    const int row = static_cast<int>((y - 665) / 18);
                    const int index = static_cast<int>(memoryOffset) + row;
                    if (row >= 0 && row < 4 &&
                        index >= 0 && index < static_cast<int>(memories.size()))
                    {
                        selectedMemory = index;
                        memoryMessage = "Memoria seleccionada: " +
                            memories[static_cast<size_t>(selectedMemory)].name;
                    }
                }
            }
            if (event.type == sf::Event::MouseButtonReleased &&
                event.mouseButton.button == sf::Mouse::Left)
            {
                treePanning = false;
                ruleScroll.endDrag();
            }
            if (event.type == sf::Event::MouseMoved)
            {
                if (treePanning)
                {
                    const sf::Vector2i current(event.mouseMove.x, event.mouseMove.y);
                    const sf::Vector2f previous = window.mapPixelToCoords(treePanAnchor, treeView);
                    const sf::Vector2f now = window.mapPixelToCoords(current, treeView);
                    treeView.move(previous - now);
                    treePanAnchor = current;
                }
                if (ruleScroll.dragging)
                    ruleScroll.dragTo(static_cast<float>(event.mouseMove.y), 445, 70,
                                      rules.size(), 70, 20);
            }
            if (event.type == sf::Event::MouseWheelScrolled)
            {
                if (event.mouseWheelScroll.x >= 400 && event.mouseWheelScroll.x <= 805 &&
                    event.mouseWheelScroll.y >= 430 && event.mouseWheelScroll.y <= 525)
                    ruleScroll.scroll(event.mouseWheelScroll.delta, rules.size(), 70, 20);
                if (event.mouseWheelScroll.x >= 825 && event.mouseWheelScroll.x <= 1260 &&
                    event.mouseWheelScroll.y >= 65 && event.mouseWheelScroll.y <= 530)
                {
                    const sf::Vector2i pixel(event.mouseWheelScroll.x, event.mouseWheelScroll.y);
                    const sf::Vector2f before = window.mapPixelToCoords(pixel, treeView);
                    treeView.zoom(event.mouseWheelScroll.delta > 0 ? 0.88f : 1.14f);
                    const sf::Vector2f after = window.mapPixelToCoords(pixel, treeView);
                    treeView.move(before - after);
                }
            }
            if (event.type == sf::Event::TextEntered && inputFocus)
            {
                if (event.text.unicode >= 32 && event.text.unicode != 127)
                {
                    std::string character;
                    appendUtf8(character, event.text.unicode);
                    editor.insert(input, character);
                }
            }
            if (event.type == sf::Event::KeyPressed && inputFocus &&
                event.key.code == sf::Keyboard::Return)
            {
                editor.insert(input, "\n");
            }
            if (event.type == sf::Event::KeyPressed && inputFocus)
            {
                if (event.key.code == sf::Keyboard::Backspace) editor.eraseBackward(input);
                else if (event.key.code == sf::Keyboard::Delete && editor.cursor < input.size())
                {
                    const size_t next = editor.cursor + 1;
                    input.erase(editor.cursor, next - editor.cursor);
                }
                else if (event.key.code == sf::Keyboard::Left) editor.moveLeft(input);
                else if (event.key.code == sf::Keyboard::Right) editor.moveRight(input);
                else if (event.key.code == sf::Keyboard::Home) editor.cursor = input.rfind('\n', editor.cursor == 0 ? 0 : editor.cursor - 1) + 1;
                else if (event.key.code == sf::Keyboard::End)
                {
                    const size_t newline = input.find('\n', editor.cursor);
                    editor.cursor = newline == std::string::npos ? input.size() : newline;
                }
            }
            if (event.type == sf::Event::KeyPressed && inputFocus &&
                event.key.code == sf::Keyboard::V &&
                (event.key.control || event.key.system))
            {
                editor.insert(input, clipboardText());
            }
        }

        window.clear(Background);
        if (!started)
        {
            drawText(window, font, "GATOCHI", 82, 105, 22, Cyan);
            drawText(window, font, "BIENVENIDO AL MOTOR DE INFERENCIA", 80, 190, 36);
            drawText(window, font, "Explora premisas, reglas y ramas semanticas.", 84, 248, 16, Muted);
            const bool hoverStart = mouse.x >= 84 && mouse.x <= 330 && mouse.y >= 330 && mouse.y <= 374;
            button(window, font, "[ ENTRAR AL MOTOR ]", 84, 330, 246, hoverStart);
            drawText(window, font, "Pulsa Enter o haz clic para comenzar", 86, 405, 13, Muted);
            window.display();
            continue;
        }
        drawText(window, font, "INFERENCE // PROPOSITIONAL LOGIC ENGINE", 20, 18, 22, Cyan);
        drawText(window, font, "5-STAGE PIPELINE  |  READY", 900, 23, 13, Green);
        drawPanel(window, 20, 65, 360, 465);
        drawPanel(window, 400, 65, 405, 465);
        drawPanel(window, 825, 65, 435, 465);
        drawText(window, font, "01  ENTRADA DE REGLAS", 35, 78, 16, Cyan);
        drawText(window, font, "Escribe una regla por linea:", 35, 102, 13, Muted);
        sf::RectangleShape editorBox(sf::Vector2f(330, 385));
        editorBox.setPosition(35, 125); editorBox.setFillColor(sf::Color(7, 11, 18));
        editorBox.setOutlineThickness(1); editorBox.setOutlineColor(inputFocus ? Cyan : sf::Color(54, 75, 96));
        window.draw(editorBox);
        const std::vector<VisualLine> entered = visualLineRanges(input, font, 13, 310);
        float textY = 138;
        size_t renderedLine = 0;
        size_t caretLine = 0;
        float caretX = 45.0f;
        for (const VisualLine& line : entered)
        {
            const bool startsPremise = renderedLine == 0 || entered[renderedLine - 1].logicalBreak;
            drawText(window, font, startsPremise ? "-> " + line.text : "   " + line.text,
                     45, textY, 13, sf::Color(220, 230, 240));
            if (editor.cursor >= line.start && editor.cursor <= line.end)
            {
                caretLine = renderedLine;
                sf::Text before(input.substr(line.start, editor.cursor - line.start), font, 13);
                caretX = 45.0f + (startsPremise ? 3.0f : 3.0f) *
                         static_cast<float>(sf::Text("-> ", font, 13).getLocalBounds().width / 3.0f) +
                         before.getLocalBounds().width;
            }
            textY += 23;
            if (textY > 488) break;
            ++renderedLine;
        }
        if (editor.focused && (caretClock.getElapsedTime().asSeconds() < 0.5f ||
                               std::fmod(caretClock.getElapsedTime().asSeconds(), 1.0f) < 0.5f))
        {
            sf::RectangleShape caret(sf::Vector2f(2, 18));
            caret.setPosition(caretX, 138.0f + static_cast<float>(caretLine) * 23.0f);
            caret.setFillColor(Cyan);
            window.draw(caret);
        }
        drawText(window, font, inputFocus ? "EDITANDO" : "PULSA ANALIZAR", 45, 490, 11, inputFocus ? Green : Muted);
        const bool hoverAnalyze = mouse.x >= 20 && mouse.x <= 190 && mouse.y >= 550 && mouse.y <= 590;
        const bool hoverSaveMemory = mouse.x >= 205 && mouse.x <= 375 && mouse.y >= 550 && mouse.y <= 590;
        const bool hoverLoadMemory = mouse.x >= 390 && mouse.x <= 560 && mouse.y >= 550 && mouse.y <= 590;
        const bool hoverDeleteMemory = mouse.x >= 20 && mouse.x <= 190 && mouse.y >= 595 && mouse.y <= 635;
        const bool hoverExport = mouse.x >= 205 && mouse.x <= 375 && mouse.y >= 595 && mouse.y <= 635;
        const bool hoverClear = mouse.x >= 390 && mouse.x <= 560 && mouse.y >= 595 && mouse.y <= 635;
        button(window, font, "[ ANALIZAR ]", 20, 550, 170, hoverAnalyze);
        button(window, font, "[ GUARDAR MEMORIA ]", 205, 550, 170, hoverSaveMemory);
        button(window, font, "[ CARGAR MEMORIA ]", 390, 550, 170, hoverLoadMemory);
        button(window, font, "[ BORRAR MEMORIA ]", 20, 595, 170, hoverDeleteMemory);
        button(window, font, "[ EXPORTAR JSON ]", 205, 595, 170, hoverExport);
        button(window, font, "[ CERRAR / LIMPIAR ]", 390, 595, 170, hoverClear);

        drawText(window, font, "02  EXTRACTOR DE ATOMOS", 415, 78, 16, Cyan);
        drawText(window, font, "Variable   Significado", 420, 108, 13, Muted);
        for (size_t i = 0; i < atoms.size() && i < 12; ++i)
        {
            std::string variable(1, static_cast<char>('p' + i));
            drawText(window, font, variable + "          " + atoms[i], 425, 132 + static_cast<float>(i) * 23, 13, Green);
        }
        drawText(window, font, "03  TRADUCTOR DE REGLAS", 415, 430, 16, Cyan);
        const float ruleTop = 453.0f;
        const float ruleHeight = 70.0f;
        const size_t firstRule = static_cast<size_t>(ruleScroll.offset / 20.0f);
        for (size_t i = firstRule; i < rules.size(); ++i)
        {
            const float y = ruleTop + static_cast<float>(i) * 20.0f - ruleScroll.offset;
            if (y < ruleTop - 18.0f || y > ruleTop + ruleHeight) continue;
            drawText(window, font, "R" + std::to_string(i + 1) + "  " + rules[i].formal,
                     425, y, 13);
        }
        sf::RectangleShape ruleTrack(sf::Vector2f(15, ruleHeight));
        ruleTrack.setPosition(780, ruleTop - 8);
        ruleTrack.setFillColor(sf::Color(9, 15, 23));
        ruleTrack.setOutlineThickness(1);
        ruleTrack.setOutlineColor(sf::Color(54, 75, 96));
        window.draw(ruleTrack);
        const float ruleMax = ruleScroll.maxOffset(rules.size(), ruleHeight, 20.0f);
        const float thumbHeight = ruleMax == 0.0f ? ruleHeight :
            std::max(18.0f, ruleHeight * ruleHeight / (rules.size() * 20.0f));
        sf::RectangleShape ruleThumb(sf::Vector2f(15, thumbHeight));
        ruleThumb.setPosition(780, ruleTop - 8 +
            (ruleMax == 0.0f ? 0.0f : (ruleHeight - thumbHeight) * ruleScroll.offset / ruleMax));
        ruleThumb.setFillColor(ruleScroll.dragging ? Cyan : PanelLight);
        window.draw(ruleThumb);

        drawText(window, font, "04  ARBOL SEMANTICO / TABLEAU", 840, 78, 16, Cyan);
        drawText(window, font, "Enramajes y hojas del arbol:", 850, 105, 12, Muted);
        window.setView(treeView);
        sf::RectangleShape root(sf::Vector2f(210, 28));
        root.setPosition(937, 125);
        root.setFillColor(PanelLight);
        root.setOutlineThickness(1);
        root.setOutlineColor(Cyan);
        window.draw(root);
        drawText(window, font, analyzed ? "REGLAS / RAIZ" : "SIN ANALISIS", 963, 131, 12);

        const size_t visibleBranches = tableau.size();
        const float firstLeafX = 700.0f;
        const float leafY = 300.0f;
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
                                            125.0f;
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

                sf::RectangleShape leaf(sf::Vector2f(110, 62));
                leaf.setPosition(x - 55, leafY);
                leaf.setFillColor(branch.closed ? sf::Color(62, 28, 38) : sf::Color(24, 58, 49));
                leaf.setOutlineThickness(1);
                leaf.setOutlineColor(branch.closed ? Red : Green);
                window.draw(leaf);
                drawText(window, font, "B" + std::to_string(branchIndex + 1),
                         x - 48, leafY + 5, 10, branch.closed ? Red : Green);
                drawText(window, font, branch.closed ? "PODADA [X]" : "ABIERTA [ ]",
                         x - 48, leafY + 20, 9, branch.closed ? Red : Green);
                std::string literals = branchLiterals(branch, atoms);
                if (literals.size() > 17) literals = literals.substr(0, 14) + "...";
                sf::Text literalText(literals, font, 9);
                literalText.setFillColor(sf::Color::White);
                const float literalWidth = literalText.getLocalBounds().width;
                literalText.setPosition(x - literalWidth / 2.0f, leafY + 37);
                window.draw(literalText);
            }
        }
        window.setView(window.getDefaultView());
        drawText(window, font, "Verde = abierta    Rojo/X = cerrada", 850, 320, 11, Muted);
        if (!tooltip.empty())
        {
            sf::RectangleShape tooltipBox(sf::Vector2f(395, 78));
            tooltipBox.setPosition(840, 325);
            tooltipBox.setFillColor(sf::Color(8, 14, 22, 245));
            tooltipBox.setOutlineThickness(1);
            tooltipBox.setOutlineColor(Cyan);
            window.draw(tooltipBox);
            drawText(window, font, tooltip, 848, 333, 10, sf::Color::White);
        }
        drawText(window, font, "05  REPORTE DE MODELOS", 840, 410, 16, Cyan);
        drawText(window, font, "ESTADO:", 850, 445, 13, Muted);
        drawText(window, font, satisfiable ? "SATISFACIBLE" : "INSATISFACIBLE", 930, 443, 17, satisfiable ? Green : Red);
        drawText(window, font, "Modelos que cumplen todas las reglas:", 850, 475, 12, Muted);
        for (size_t i = 0; i < models.size() && i < 8; ++i)
        {
            std::string row = "M" + std::to_string(i + 1) + "  ";
            for (size_t atomIndex = 0; atomIndex < atoms.size(); ++atomIndex)
                row += std::string(1, static_cast<char>('p' + atomIndex)) + "=" +
                       (models[i].values.at(atoms[atomIndex]) ? "V  " : "F  ");
            drawText(window, font, row, 855, 500 + static_cast<float>(i) * 18, 12, Green);
        }
        drawPanel(window, 20, 640, 785, 105);
        drawText(window, font, "06  MEMORIAS GUARDADAS", 35, 650, 14, Cyan);
        drawText(window, font, "Selecciona una memoria y pulsa CARGAR o BORRAR.", 260, 651, 11, Muted);
        const size_t visibleMemories = std::min<size_t>(4, memories.size() - std::min(memoryOffset, memories.size()));
        const size_t startMemory = std::min(memoryOffset, memories.size());
        for (size_t i = 0; i < visibleMemories; ++i)
        {
            const size_t memoryIndex = startMemory + i;
            sf::RectangleShape row(sf::Vector2f(710, 17));
            row.setPosition(30, 665 + static_cast<float>(i) * 18);
            const bool selected = selectedMemory == static_cast<int>(memoryIndex);
            row.setFillColor(selected ? PanelLight : sf::Color(14, 21, 30));
            row.setOutlineThickness(1);
            row.setOutlineColor(selected ? Cyan : sf::Color(40, 58, 78));
            window.draw(row);
            std::string snippet = memories[memoryIndex].input;
            snippet = replaceAll(snippet, "\n", " / ");
            if (snippet.size() > 78) snippet = snippet.substr(0, 75) + "...";
            drawText(window, font, memories[memoryIndex].name + "  ->  " + snippet,
                     36, 668 + static_cast<float>(i) * 18, 10,
                     selected ? Cyan : sf::Color::White);
        }
        if (memories.empty())
            drawText(window, font, "Sin memorias guardadas.", 35, 670, 11, Muted);
        button(window, font, "^", 760, 665, 30, memoryOffset > 0);
        button(window, font, "v", 760, 700, 30, memoryOffset + 4 < memories.size());
        drawText(window, font, memoryMessage, 35, 725, 11, Muted);
        drawText(window, font, "Guardar crea: analisis_proposicional.json", 840, 505, 11, Muted);
        window.display();
    }
    for (Rule& rule : rules) deleteFormula(rule.formula);
    return 0;
}
