// References for citations (bib.h): the item model, and the BibTeX and
// CSL-JSON readers that fill it.

#pragma optimize("s", on)

#include "bib.h"

namespace bib {

const std::string* Item::field(std::string_view variable) const {
    auto it = fields.find(std::string(variable));
    return it == fields.end() || it->second.empty() ? nullptr : &it->second;
}

const std::vector<Name>* Item::namesOf(std::string_view variable) const {
    auto it = names.find(std::string(variable));
    return it == names.end() || it->second.empty() ? nullptr : &it->second;
}

const Date* Item::date(std::string_view variable) const {
    auto it = dates.find(std::string(variable));
    return it == dates.end() || it->second.empty() ? nullptr : &it->second;
}

const Item* Library::find(std::string_view id) const {
    auto it = index.find(std::string(id));
    return it == index.end() ? nullptr : &items[it->second];
}

void Library::add(Item item) {
    if (index.count(item.id)) {
        diagnostics.push_back({item.source, item.line, "duplicate key @" + item.id + "; the first is used"});
        return;
    }
    index.emplace(item.id, items.size());
    items.push_back(std::move(item));
}

}
