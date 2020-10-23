/**
 * Copyright (C) 2017 MongoDB Inc.
 *
 * This program is free software: you can redistribute it and/or  modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, the copyright holders give permission to link the
 * code of portions of this program with the OpenSSL library under certain
 * conditions as described in each individual source file and distribute
 * linked combinations including the program with the OpenSSL library. You
 * must comply with the GNU Affero General Public License in all respects
 * for all of the code used other than as permitted herein. If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so. If you do not
 * wish to do so, delete this exception statement from your version. If you
 * delete this exception statement from all source files in the program,
 * then also delete it in the license file.
 */

#include "mongo/platform/basic.h"

#include "mongo/db/matcher/schema/expression_internal_schema_allowed_properties.h"

namespace mongo {
constexpr StringData InternalSchemaAllowedPropertiesMatchExpression::kName;

void InternalSchemaAllowedPropertiesMatchExpression::init(
    boost::container::flat_set<StringData> properties,
    StringData namePlaceholder,
    std::vector<PatternSchema> patternProperties,
    std::unique_ptr<ExpressionWithPlaceholder> otherwise) {
    _properties = std::move(properties);
    _namePlaceholder = namePlaceholder;
    _patternProperties = std::move(patternProperties);
    _otherwise = std::move(otherwise);
}

void InternalSchemaAllowedPropertiesMatchExpression::debugString(StringBuilder& debug,
                                                                 int level) const {
    _debugAddSpace(debug, level);

    BSONObjBuilder builder;
    serialize(&builder);
    debug << builder.obj().toString() << "\n";

    const auto* tag = getTag();
    if (tag) {
        debug << " ";
        tag->debugString(&debug);
    }

    debug << "\n";
}

bool InternalSchemaAllowedPropertiesMatchExpression::equivalent(const MatchExpression* expr) const {
    if (matchType() != expr->matchType()) {
        return false;
    }

    const auto* other = static_cast<const InternalSchemaAllowedPropertiesMatchExpression*>(expr);
    return _properties == other->_properties && _namePlaceholder == other->_namePlaceholder &&
        _otherwise->equivalent(other->_otherwise.get()) &&
        std::is_permutation(_patternProperties.begin(),
                            _patternProperties.end(),
                            other->_patternProperties.begin(),
                            other->_patternProperties.end(),
                            [](const auto& expr1, const auto& expr2) {
                                return expr1.first.rawRegex == expr2.first.rawRegex &&
                                    expr1.second->equivalent(expr2.second.get());
                            });
}

bool InternalSchemaAllowedPropertiesMatchExpression::matches(const MatchableDocument* doc,
                                                             MatchDetails* details) const {
    return _matchesBSONObj(doc->toBSON());
}

bool InternalSchemaAllowedPropertiesMatchExpression::matchesSingleElement(const BSONElement& elem,
                                                                          MatchDetails*) const {
    if (elem.type() != BSONType::Object) {
        return false;
    }

    return _matchesBSONObj(elem.embeddedObject());
}

bool InternalSchemaAllowedPropertiesMatchExpression::_matchesBSONObj(const BSONObj& obj) const {
    for (auto&& property : obj) {
        bool checkOtherwise = true;
        for (auto&& constraint : _patternProperties) {
            if (constraint.first.regex->PartialMatch(property.fieldName())) {
                checkOtherwise = false;
                if (!constraint.second->matchesBSONElement(property)) {
                    return false;
                }
            }
        }

        if (checkOtherwise &&
            _properties.find(property.fieldNameStringData()) != _properties.end()) {
            checkOtherwise = false;
        }

        if (checkOtherwise && !_otherwise->matchesBSONElement(property)) {
            return false;
        }
    }
    return true;
}

void InternalSchemaAllowedPropertiesMatchExpression::serialize(BSONObjBuilder* builder) const {
    BSONObjBuilder expressionBuilder(
        builder->subobjStart(InternalSchemaAllowedPropertiesMatchExpression::kName));

    BSONArrayBuilder propertiesBuilder(expressionBuilder.subarrayStart("properties"));
    for (auto&& property : _properties) {
        propertiesBuilder.append(property);
    }
    propertiesBuilder.doneFast();

    expressionBuilder.append("namePlaceholder", _namePlaceholder);

    BSONArrayBuilder patternPropertiesBuilder(expressionBuilder.subarrayStart("patternProperties"));
    for (auto&& item : _patternProperties) {
        BSONObjBuilder itemBuilder(patternPropertiesBuilder.subobjStart());
        itemBuilder.appendRegex("regex", item.first.rawRegex);

        BSONObjBuilder subexpressionBuilder(itemBuilder.subobjStart("expression"));
        item.second->getFilter()->serialize(&subexpressionBuilder);
        subexpressionBuilder.doneFast();
    }
    patternPropertiesBuilder.doneFast();

    BSONObjBuilder otherwiseBuilder(expressionBuilder.subobjStart("otherwise"));
    _otherwise->getFilter()->serialize(&otherwiseBuilder);
    otherwiseBuilder.doneFast();
    expressionBuilder.doneFast();
}

std::unique_ptr<MatchExpression> InternalSchemaAllowedPropertiesMatchExpression::shallowClone()
    const {
    std::vector<PatternSchema> clonedPatternProperties;
    clonedPatternProperties.reserve(_patternProperties.size());
    for (auto&& constraint : _patternProperties) {
        clonedPatternProperties.emplace_back(Pattern(constraint.first.rawRegex),
                                             constraint.second->shallowClone());
    }

    auto clone = stdx::make_unique<InternalSchemaAllowedPropertiesMatchExpression>();
    clone->init(_properties,
                _namePlaceholder,
                std::move(clonedPatternProperties),
                _otherwise->shallowClone());
    return {std::move(clone)};
}
}  // namespace mongo
