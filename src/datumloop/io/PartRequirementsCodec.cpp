#include "datumloop/io/PartRequirementsCodec.hpp"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace datumloop {
namespace {
void need(bool b) { if (!b) throw std::invalid_argument("invalid part-requirements JSON field or schema"); }
QString q(const std::string& s) { return QString::fromStdString(s); }
std::string text(const QJsonObject& o, const char* key) { auto v=o.value(QLatin1String(key)); need(v.isString()); return v.toString().toStdString(); }
double number(const QJsonObject& o, const char* key) { auto v=o.value(QLatin1String(key)); need(v.isDouble() && std::isfinite(v.toDouble())); return v.toDouble(); }
bool boolean(const QJsonObject& o, const char* key) { auto v=o.value(QLatin1String(key)); need(v.isBool()); return v.toBool(); }
QJsonObject object(const QJsonValue& v) { need(v.isObject()); return v.toObject(); }
QJsonArray array(const QJsonObject& o, const char* key) { auto v=o.value(QLatin1String(key)); need(v.isArray()); return v.toArray(); }
std::optional<double> optionalNumber(const QJsonObject& o, const char* key) { auto v=o.value(QLatin1String(key)); return v.isNull() || v.isUndefined() ? std::nullopt : std::optional{number(o,key)}; }
std::optional<std::string> optionalText(const QJsonObject& o, const char* key) { auto v=o.value(QLatin1String(key)); return v.isNull() || v.isUndefined() ? std::nullopt : std::optional{text(o,key)}; }
QJsonValue json(const std::optional<double>& value) { return value ? QJsonValue(*value) : QJsonValue(QJsonValue::Null); }
QJsonValue json(const std::optional<std::string>& value) { return value ? QJsonValue(q(*value)) : QJsonValue(QJsonValue::Null); }
QJsonArray strings(const std::vector<std::string>& values) { QJsonArray a; for(const auto& v:values) a.append(q(v)); return a; }
std::vector<std::string> strings(const QJsonObject& o,const char* key) { std::vector<std::string> result; for(const auto& v:array(o,key)) { need(v.isString()); result.push_back(v.toString().toStdString()); } return result; }
template<class E> E enumeration(const QJsonObject& o, const char* key) {
    const auto name=text(o,key);
    for(int i=0;i<static_cast<int>(E::Count);++i) if(partDesignName(static_cast<E>(i))==name) return static_cast<E>(i);
    throw std::invalid_argument("unknown part-requirements enumeration: "+name);
}
QJsonObject encodeEvidence(const RequirementEvidence& e) {
    return {{"sourceId",q(e.sourceId)}, {"pdfPage",e.pdfPage ? QJsonValue(*e.pdfPage) : QJsonValue(QJsonValue::Null)},
        {"section",q(e.section)}, {"kind",q(partDesignName(e.kind))}, {"interpretation",q(e.interpretation)},
        {"reviewed",e.reviewed}, {"derivedFrom",strings(e.derivedFrom)}, {"derivation",json(e.derivation)}};
}
RequirementEvidence evidence(const QJsonObject& parent) {
    const auto o=object(parent.value("evidence")); RequirementEvidence e;
    e.sourceId=text(o,"sourceId"); e.section=text(o,"section"); e.interpretation=text(o,"interpretation");
    e.kind=enumeration<RequirementEvidenceKind>(o,"kind"); e.reviewed=boolean(o,"reviewed");
    auto page=optionalNumber(o,"pdfPage");
    if(page) { need(*page>=1 && *page<=std::numeric_limits<int>::max() && std::floor(*page)==*page); e.pdfPage=static_cast<int>(*page); }
    e.derivedFrom=strings(o,"derivedFrom"); e.derivation=optionalText(o,"derivation"); return e;
}
} // namespace

QJsonObject encodePartRequirements(const PartRequirements& part) {
    part.validate(); QJsonArray sources, requirements, variables, interfaces, constraints, changes;
    for(const auto& s:part.sources) sources.append(QJsonObject{{"id",q(s.id)}, {"type",q(partDesignName(s.type))},
        {"title",q(s.title)}, {"manufacturer",q(s.manufacturer)}, {"partConfiguration",q(s.partConfiguration)},
        {"revision",q(s.revision)}, {"reference",q(s.reference)}, {"sha256",json(s.sha256)}});
    for(const auto& r:part.requirements) requirements.append(QJsonObject{{"id",q(r.id)}, {"description",q(r.description)},
        {"status",q(partDesignName(r.status))}, {"value",r.textValue ? QJsonValue(q(*r.textValue)) : json(r.value)}, {"unit",q(r.unit)},
        {"lowerDeviation",json(r.lowerDeviation)}, {"upperDeviation",json(r.upperDeviation)},
        {"critical",r.critical}, {"evidence",encodeEvidence(r.evidence)}});
    for(const auto& v:part.variables) variables.append(QJsonObject{{"id",q(v.id)}, {"requirementId",q(v.requirementId)},
        {"role",q(partDesignName(v.role))}, {"value",v.value}, {"minimum",json(v.minimum)},
        {"maximum",json(v.maximum)}, {"evidence",encodeEvidence(v.evidence)}});
    for(const auto& i:part.interfaces) interfaces.append(QJsonObject{{"id",q(i.id)}, {"name",q(i.name)},
        {"matingPart",q(i.matingPart)}, {"parameterIds",strings(i.parameterIds)}, {"requirementIds",strings(i.requirementIds)},
        {"topologyReferenceIds",strings(i.topologyReferenceIds)}});
    for(const auto& c:part.constraints) constraints.append(QJsonObject{{"id",q(c.id)}, {"requirementId",q(c.requirementId)},
        {"metric",q(c.metric)}, {"relation",q(partDesignName(c.relation))}});
    for(const auto& c:part.changes) changes.append(QJsonObject{{"parameterId",q(c.parameterId)},
        {"previousValue",c.previousValue}, {"nextValue",c.nextValue}, {"engineeringReason",q(c.engineeringReason)}});
    return {{"schemaVersion",1}, {"id",q(part.id)}, {"name",q(part.name)}, {"revision",q(part.revision)},
        {"sources",sources}, {"requirements",requirements}, {"variables",variables}, {"interfaces",interfaces},
        {"constraints",constraints}, {"changes",changes}};
}
PartRequirements decodePartRequirements(const QJsonObject& o) {
    need(number(o,"schemaVersion")==1); PartRequirements p;
    p.id=text(o,"id"); p.name=text(o,"name"); p.revision=text(o,"revision");
    for(const auto& item:array(o,"sources")) {
        const auto s=object(item);
        p.sources.push_back({text(s,"id"),enumeration<RequirementSourceType>(s,"type"),text(s,"title"),
            text(s,"manufacturer"),text(s,"partConfiguration"),text(s,"revision"),text(s,"reference"),optionalText(s,"sha256")});
    }
    for(const auto& item:array(o,"requirements")) {
        const auto r=object(item);
        Requirement record;
        record.id=text(r,"id"); record.description=text(r,"description"); record.status=enumeration<RequirementStatus>(r,"status");
        if(r.value("value").isString()) record.textValue=text(r,"value"); else record.value=optionalNumber(r,"value");
        record.unit=text(r,"unit"); record.lowerDeviation=optionalNumber(r,"lowerDeviation"); record.upperDeviation=optionalNumber(r,"upperDeviation");
        record.critical=boolean(r,"critical"); record.evidence=evidence(r); p.requirements.push_back(std::move(record));
    }
    for(const auto& item:array(o,"variables")) {
        const auto v=object(item);
        p.variables.push_back({text(v,"id"),text(v,"requirementId"),enumeration<ParameterRole>(v,"role"),number(v,"value"),
            optionalNumber(v,"minimum"),optionalNumber(v,"maximum"),evidence(v)});
    }
    for(const auto& item:array(o,"interfaces")) {
        const auto i=object(item);
        MechanicalInterface interface;
        interface.id=text(i,"id"); interface.name=text(i,"name"); interface.matingPart=text(i,"matingPart");
        interface.parameterIds=strings(i,"parameterIds"); interface.requirementIds=strings(i,"requirementIds");
        interface.topologyReferenceIds=strings(i,"topologyReferenceIds"); p.interfaces.push_back(std::move(interface));
    }
    for(const auto& item:array(o,"constraints")) {
        const auto c=object(item);
        p.constraints.push_back({text(c,"id"),text(c,"requirementId"),text(c,"metric"),enumeration<DesignConstraintRelation>(c,"relation")});
    }
    for(const auto& item:array(o,"changes")) {
        const auto c=object(item);
        p.changes.push_back({text(c,"parameterId"),number(c,"previousValue"),number(c,"nextValue"),text(c,"engineeringReason")});
    }
    p.validate(); return p;
}
PartRequirements loadPartRequirements(const QString& path) {
    QFile file(path); if(!file.open(QIODevice::ReadOnly)) throw std::runtime_error("could not open part requirements");
    QJsonParseError error; const auto doc=QJsonDocument::fromJson(file.readAll(),&error);
    need(error.error==QJsonParseError::NoError && doc.isObject()); return decodePartRequirements(doc.object());
}
} // namespace datumloop
