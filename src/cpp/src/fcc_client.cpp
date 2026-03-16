#include "fcc/fcc_client.h"
#include <sstream>
#include <algorithm>

namespace fcc {

FCCClient::FCCClient()
    : poll_interval_seconds_(900)  // Default 15 min
    , last_poll_timestamp_("")
{}

FCCClient::~FCCClient() = default;

void FCCClient::set_api_key(const std::string& key) {
    api_key_ = key;
}

void FCCClient::set_poll_interval_seconds(uint32_t seconds) {
    poll_interval_seconds_ = seconds;
}

std::vector<Filing> FCCClient::query_filings(const ECFSQuery& query) {
    std::string url = build_ecfs_url(query);
    std::string response = http_get(url);
    // TODO: Parse JSON response into Filing structs
    std::vector<Filing> filings;
    return filings;
}

std::vector<Filing> FCCClient::get_recent_filings(uint32_t hours, uint32_t limit) {
    ECFSQuery query;
    query.limit = limit;
    // TODO: Set date_from to now - hours
    return query_filings(query);
}

std::vector<Filing> FCCClient::get_satellite_filings(uint32_t hours) {
    ECFSQuery query;
    query.satellite_only = true;
    query.bureau = "Space";
    return query_filings(query);
}

Filing FCCClient::get_filing_by_id(const std::string& filing_id) {
    std::string url = "https://publicapi.fcc.gov/ecfs/filings/" + filing_id;
    if (!api_key_.empty()) url += "?api_key=" + api_key_;
    std::string response = http_get(url);
    return parse_ecfs_filing(response);
}

std::vector<Filing> FCCClient::get_docket_filings(const std::string& docket_number) {
    ECFSQuery query;
    query.docket_number = docket_number;
    return query_filings(query);
}

std::vector<SatelliteLicense> FCCClient::get_satellite_licenses(const std::string& status) {
    // Query ULS for Part 25 (satellite) service codes
    std::string url = "https://publicapi.fcc.gov/uls/licenses?"
                      "serviceCode=NN&serviceCode=SA&serviceCode=SE";
    if (!status.empty()) url += "&status=" + status;
    if (!api_key_.empty()) url += "&api_key=" + api_key_;
    std::string response = http_get(url);
    // TODO: Parse response
    std::vector<SatelliteLicense> licenses;
    return licenses;
}

std::vector<SatelliteLicense> FCCClient::get_ngso_constellations() {
    auto all = get_satellite_licenses("");
    std::vector<SatelliteLicense> ngso;
    std::copy_if(all.begin(), all.end(), std::back_inserter(ngso),
                 [](const SatelliteLicense& l) { return l.orbit_type == "NGSO"; });
    return ngso;
}

SatelliteLicense FCCClient::get_license_by_callsign(const std::string& call_sign) {
    std::string url = "https://publicapi.fcc.gov/uls/licenses?callSign=" + call_sign;
    if (!api_key_.empty()) url += "&api_key=" + api_key_;
    std::string response = http_get(url);
    return parse_satellite_license(response);
}

std::vector<SpectrumAllocation> FCCClient::get_allocations(double freq_lower_mhz,
                                                            double freq_upper_mhz) {
    std::ostringstream url;
    url << "https://publicapi.fcc.gov/spectrum-manager/allocations?"
        << "freqLower=" << freq_lower_mhz
        << "&freqUpper=" << freq_upper_mhz;
    if (!api_key_.empty()) url << "&api_key=" << api_key_;
    std::string response = http_get(url.str());
    // TODO: Parse response
    std::vector<SpectrumAllocation> allocations;
    return allocations;
}

std::vector<SpectrumAllocation> FCCClient::get_proposed_reallocations() {
    // Look for NPRMs in spectrum-related proceedings
    ECFSQuery query;
    query.type = FilingType::NPRM;
    query.spectrum_only = true;
    auto filings = query_filings(query);
    // TODO: Extract spectrum allocation proposals from filings
    std::vector<SpectrumAllocation> proposals;
    return proposals;
}

std::vector<ULSRecord> FCCClient::search_uls(const std::string& query) {
    std::string url = build_uls_url(query);
    std::string response = http_get(url);
    // TODO: Parse response
    std::vector<ULSRecord> records;
    return records;
}

std::vector<ULSRecord> FCCClient::get_uls_by_callsign(const std::string& call_sign) {
    return search_uls("callSign=" + call_sign);
}

std::vector<ULSRecord> FCCClient::get_uls_by_licensee(const std::string& licensee) {
    return search_uls("licensee=" + licensee);
}

std::vector<ExperimentalLicense> FCCClient::get_experimental_licenses(uint32_t hours) {
    // Query OET experimental licensing system
    std::string url = "https://apps.fcc.gov/oetcf/els/reports/442_Export.cfm?"
                      "format=json";
    std::string response = http_get(url);
    // TODO: Parse and filter by date
    std::vector<ExperimentalLicense> licenses;
    return licenses;
}

std::vector<ExperimentalLicense> FCCClient::get_space_experimental_licenses() {
    auto all = get_experimental_licenses(168);
    std::vector<ExperimentalLicense> space;
    std::copy_if(all.begin(), all.end(), std::back_inserter(space),
                 [](const ExperimentalLicense& l) { return l.is_space_related; });
    return space;
}

std::vector<EquipmentAuth> FCCClient::search_equipment(const std::string& query) {
    std::string url = "https://publicapi.fcc.gov/equipment/search?query=" + query;
    if (!api_key_.empty()) url += "&api_key=" + api_key_;
    std::string response = http_get(url);
    // TODO: Parse response
    std::vector<EquipmentAuth> auths;
    return auths;
}

EquipmentAuth FCCClient::get_equipment_by_fccid(const std::string& fcc_id) {
    std::string url = "https://publicapi.fcc.gov/equipment/fccid/" + fcc_id;
    if (!api_key_.empty()) url += "?api_key=" + api_key_;
    std::string response = http_get(url);
    return parse_equipment(response);
}

std::vector<EquipmentAuth> FCCClient::get_satellite_terminals() {
    return search_equipment("satellite terminal");
}

std::vector<FilingAlert> FCCClient::check_for_alerts(uint32_t hours) {
    auto filings = get_recent_filings(hours, 200);
    std::vector<FilingAlert> alerts;
    for (const auto& f : filings) {
        if (is_significant_filing(f)) {
            alerts.push_back(classify_filing(f));
        }
    }
    return alerts;
}

bool FCCClient::is_significant_filing(const Filing& filing) {
    // Significant if: satellite-related, spectrum reallocation, NPRM, or major order
    if (filing.is_satellite_related) return true;
    if (filing.type == FilingType::NPRM) return true;
    if (filing.type == FilingType::ORDER) return true;
    if (filing.type == FilingType::SPECTRUM_ALLOCATION) return true;
    if (filing.type == FilingType::AUCTION_RESULT) return true;
    return false;
}

std::string FCCClient::determine_severity(const Filing& filing) {
    if (filing.type == FilingType::ORDER ||
        filing.type == FilingType::SPECTRUM_ALLOCATION ||
        filing.type == FilingType::AUCTION_RESULT) {
        return "flash";
    }
    if (filing.type == FilingType::NPRM ||
        filing.type == FilingType::SATELLITE_LICENSE) {
        return "priority";
    }
    return "routine";
}

FilingAlert FCCClient::classify_filing(const Filing& filing) {
    FilingAlert alert;
    alert.filing_id = filing.filing_id;
    alert.docket_number = filing.docket_number;
    alert.severity = determine_severity(filing);
    alert.title = filing.proceeding_name;
    alert.summary = filing.description;
    alert.timestamp = filing.date_filed;

    // Categorize
    if (filing.is_satellite_related) {
        alert.category = "satellite_filing";
    } else if (filing.is_spectrum_related) {
        alert.category = "spectrum_change";
    } else if (filing.type == FilingType::NPRM) {
        alert.category = "regulatory_proposal";
    } else {
        alert.category = "general";
    }

    return alert;
}

std::string FCCClient::build_ecfs_url(const ECFSQuery& query) {
    std::ostringstream url;
    url << "https://publicapi.fcc.gov/ecfs/filings?";
    if (!query.docket_number.empty())
        url << "proceedings.name=" << query.docket_number << "&";
    if (!query.filer_name.empty())
        url << "filers.name=" << query.filer_name << "&";
    if (!query.date_from.empty())
        url << "date_received=[gte]" << query.date_from << "&";
    if (!query.date_to.empty())
        url << "date_received=[lte]" << query.date_to << "&";
    if (!query.bureau.empty())
        url << "bureau=" << query.bureau << "&";
    url << "limit=" << (query.limit > 0 ? query.limit : 25);
    url << "&offset=" << query.offset;
    if (!api_key_.empty())
        url << "&api_key=" << api_key_;
    return url.str();
}

std::string FCCClient::build_uls_url(const std::string& query) {
    return "https://publicapi.fcc.gov/uls/licenses?" + query +
           (api_key_.empty() ? "" : "&api_key=" + api_key_);
}

std::string FCCClient::http_get(const std::string& url) {
    // Stub — in WASM, this will use emscripten_fetch or JS bridge
    return "{}";
}

// Parse stubs — implement with JSON parser (nlohmann/json or custom)
Filing FCCClient::parse_ecfs_filing(const std::string& json_record) {
    Filing f;
    f.type = FilingType::UNKNOWN;
    return f;
}

SatelliteLicense FCCClient::parse_satellite_license(const std::string& json_record) {
    SatelliteLicense l;
    l.subtype = SatelliteFilingType::UNKNOWN;
    return l;
}

SpectrumAllocation FCCClient::parse_allocation(const std::string& json_record) {
    SpectrumAllocation a;
    a.band = SpectrumBand::UNKNOWN;
    return a;
}

ULSRecord FCCClient::parse_uls_record(const std::string& json_record) {
    return ULSRecord{};
}

ExperimentalLicense FCCClient::parse_experimental(const std::string& json_record) {
    return ExperimentalLicense{};
}

EquipmentAuth FCCClient::parse_equipment(const std::string& json_record) {
    return EquipmentAuth{};
}

}  // namespace fcc
