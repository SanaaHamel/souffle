#pragma once

#include "Util.h"
#include "profile/json11.h"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace souffle {
namespace profile {

class DirectoryEntry;
class DurationEntry;
class SizeEntry;
class TextEntry;

/**
 * Visitor Interface
 */
class Visitor {
public:
    virtual ~Visitor() = default;

    // visit entries in a directory
    virtual void visit(DirectoryEntry& e);

    // visit entries
    virtual void visit(SizeEntry& e) {}
    virtual void visit(TextEntry& e) {}
    virtual void visit(DurationEntry& e) {}
};

/**
 * Entry class
 *
 * abstract class for a key/value entry in a hierarchical database
 */
class Entry {
private:
    // entry key
    std::string key;

public:
    Entry(std::string key) : key(std::move(key)) {}
    virtual ~Entry() = default;

    // get key
    std::string& getKey() {
        return key;
    };

    // accept visitor
    virtual void accept(Visitor& v) = 0;

    // print
    virtual void print(std::ostream& os, int tabpos) = 0;
};

/**
 * DirectoryEntry entry
 */
class DirectoryEntry : public Entry {
private:
    std::map<std::string, std::unique_ptr<Entry>> entries;

public:
    DirectoryEntry(const std::string& name) : Entry(name) {}

    // get keys
    const std::set<std::string> getKeys() const {
        std::set<std::string> result;
        for (auto const& cur : entries) {
            result.insert(cur.first);
        }
        return result;
    }

    // write entry
    Entry* writeEntry(std::unique_ptr<Entry> entry) {
        assert(entry != nullptr && "null entry");
        const std::string& key = entry->getKey();
        entries[key] = std::move(entry);
        return readEntry(key);
    }

    // read entry
    Entry* readEntry(const std::string& key) const {
        auto it = entries.find(key);
        if (it != entries.end()) {
            return (*it).second.get();
        } else {
            return nullptr;
        }
    }

    // read directory
    DirectoryEntry* readDirectoryEntry(const std::string& key) const {
        return dynamic_cast<DirectoryEntry*>(readEntry(key));
    }

    // accept visitor
    void accept(Visitor& v) override {
        v.visit(*this);
    }

    // print directory
    void print(std::ostream& os, int tabpos) override {
        os << std::string(tabpos, ' ') << '"' << getKey() << "\": {" << std::endl;
        bool first{true};
        for (auto const& cur : entries) {
            if (!first) {
                os << ',' << std::endl;
            } else {
                first = false;
            }
            cur.second->print(os, tabpos + 1);
        }
        os << std::endl << std::string(tabpos, ' ') << '}';
    }
};

/**
 * SizeEntry
 */
class SizeEntry : public Entry {
private:
    size_t size;  // size
public:
    SizeEntry(const std::string& key, size_t size) : Entry(key), size(size) {}

    // get size
    size_t getSize() const {
        return size;
    }

    // accept visitor
    void accept(Visitor& v) override {
        v.visit(*this);
    }

    // print entry
    void print(std::ostream& os, int tabpos) override {
        os << std::string(tabpos, ' ') << "\"" << getKey() << "\": " << size;
    }
};

/**
 * TextEntry
 */
class TextEntry : public Entry {
private:
    // entry text
    std::string text;

public:
    TextEntry(const std::string& key, std::string text) : Entry(key), text(std::move(text)) {}

    // get text
    const std::string& getText() const {
        return text;
    }

    // accept visitor
    void accept(Visitor& v) override {
        v.visit(*this);
    }

    // write size entry
    void print(std::ostream& os, int tabpos) override {
        os << std::string(tabpos, ' ') << "\"" << getKey() << "\": \"" << text << "\"";
    }
};

/**
 * Duration Entry
 */
class DurationEntry : public Entry {
private:
    // duration start
    milliseconds start;

    // duration end
    milliseconds end;

public:
    DurationEntry(const std::string& key, milliseconds start, milliseconds end)
            : Entry(key), start(start), end(end) {}

    // get start
    milliseconds getStart() const {
        return start;
    }

    // get end
    milliseconds getEnd() const {
        return end;
    }

    // accept visitor
    void accept(Visitor& v) override {
        v.visit(*this);
    }

    // write size entry
    void print(std::ostream& os, int tabpos) override {
        os << std::string(tabpos, ' ') << '"' << getKey();
        os << "\": { \"start\": ";
        os << start.count();
        os << ", \"end\": ";
        os << end.count();
        os << '}';
    }
};

void Visitor::visit(DirectoryEntry& e) {
    std::cout << "Dir " << e.getKey() << "\n";
    for (const auto& cur : e.getKeys()) {
        std::cout << "\t :" << cur << "\n";
        e.readEntry(cur)->accept(*this);
    }
}

class Counter : public Visitor {
private:
    size_t ctr{0};
    std::string key;

public:
    Counter(std::string key) : key(std::move(key)) {}
    void visit(SizeEntry& e) override {
        std::cout << "Size entry : " << e.getKey() << " " << e.getSize() << "\n";
        if (e.getKey() == key) {
            ctr += e.getSize();
        }
    }
    size_t getCounter() const {
        return ctr;
    }
};

/**
 * Hierarchical databas
 */
class ProfileDatabase {
private:
    std::unique_ptr<DirectoryEntry> root;

protected:
    /**
     * Find path: if directories along the path do not exist, create them.
     */
    DirectoryEntry* lookupPath(std::vector<std::string> path) {
        DirectoryEntry* dir = root.get();
        for (const std::string& key : path) {
            DirectoryEntry* newDir = dir->readDirectoryEntry(key);
            if (newDir == nullptr) {
                newDir = dynamic_cast<DirectoryEntry*>(
                        dir->writeEntry(std::unique_ptr<DirectoryEntry>(new DirectoryEntry(key))));
            }
            dir = newDir;
        }
        return dir;
    }

    void parseJson(const json11::Json& json, std::unique_ptr<DirectoryEntry>& node) {
        for (auto& cur : json.object_items()) {
            if (cur.second.is_object()) {
                std::string err;
                // Duration entries are also maps
                if (cur.second.has_shape(
                            {{"start", json11::Json::NUMBER}, {"end", json11::Json::NUMBER}}, err)) {
                    node->writeEntry(std::make_unique<DurationEntry>(cur.first, cur.second.string_value()));
                } else {
                    auto dir = std::make_unique<DirectoryEntry>(node->getKey() + cur.first);
                    parseJson(cur.second, dir);
                    node->writeEntry(std::move(dir));
                }
            } else if (cur.second.is_string()) {
                node->writeEntry(std::make_unique<TextEntry>(cur.first, cur.second.string_value()));
            } else if (cur.second.is_number()) {
                node->writeEntry(std::make_unique<SizeEntry>(cur.first, cur.second.int_value()));
            } else {
                std::string err;
                cur.second.dump(err);
                std::cerr << "Unknown types in profile log: " << cur.first << ": " << err << std::endl;
            }
        }
    }

public:
    ProfileDatabase() : root(std::unique_ptr<DirectoryEntry>(new DirectoryEntry("root"))) {}
    ProfileDatabase(const std::string& filename) : root(std::make_unique<DirectoryEntry>("root")) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Log file could not be opened." << std::endl;
            exit(1);
        }
        std::string jsonString((std::istreambuf_iterator<char>(file)), (std::istreambuf_iterator<char>()));
        std::string error;
        json11::Json json = json11::Json::parse(jsonString, error);

        parseJson(json["root"], root);
    }

    // add size entry
    void addSizeEntry(std::vector<std::string> qualifier, size_t size) {
        assert(qualifier.size() > 0 && "no qualifier");
        std::vector<std::string> path(qualifier.begin(), qualifier.end() - 1);
        DirectoryEntry* dir = lookupPath(path);

        const std::string& key = qualifier.back();
        std::unique_ptr<SizeEntry> entry = std::make_unique<SizeEntry>(key, size);
        dir->writeEntry(std::move(entry));
    }

    // add text entry
    void addTextEntry(std::vector<std::string> qualifier, const std::string& text) {
        assert(qualifier.size() > 0 && "no qualifier");
        std::vector<std::string> path(qualifier.begin(), qualifier.end() - 1);
        DirectoryEntry* dir = lookupPath(path);

        const std::string& key = qualifier.back();
        std::unique_ptr<TextEntry> entry = std::make_unique<TextEntry>(key, text);
        dir->writeEntry(std::move(entry));
    }

    // add duration entry
    void addDurationEntry(std::vector<std::string> qualifier, milliseconds start, milliseconds end) {
        assert(qualifier.size() > 0 && "no qualifier");
        std::vector<std::string> path(qualifier.begin(), qualifier.end() - 1);
        DirectoryEntry* dir = lookupPath(path);

        const std::string& key = qualifier.back();
        std::unique_ptr<DurationEntry> entry = std::make_unique<DurationEntry>(key, start, end);
        dir->writeEntry(std::move(entry));
    }

    // compute sum
    size_t computeSum(const std::vector<std::string>& qualifier) {
        assert(qualifier.size() > 0 && "no qualifier");
        std::vector<std::string> path(qualifier.begin(), qualifier.end() - 1);
        DirectoryEntry* dir = lookupPath(path);

        const std::string& key = qualifier.back();
        std::cout << "Key: " << key << std::endl;
        Counter ctr(key);
        dir->accept(ctr);
        return ctr.getCounter();
    }

    // print database
    void print(std::ostream& os) {
        os << '{' << std::endl;
        root->print(os, 1);
        os << std::endl << '}' << std::endl;
    };
};

}  // namespace profile
}  // namespace souffle
