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
    // FCC ECFS (Electronic Comment Filing System) API
    // Reference: https://www.fcc.gov/ecfs/public-api-docs
    // Response: JSON with "filings" array, each containing:
    //   id_filing, proceeding.name, filers[].name, date_submission,
    //   date_received, bureau.name, documents[].url, text_data
    std::string url = build_ecfs_url(query);
    std::string response = http_get(url);

    std::vector<Filing> filings;

    // Parse JSON "filings" array
    auto filings_pos = response.find("\"filings\"");
    if (filings_pos == std::string::npos) return filings;

    auto arr_start = response.find('[', filings_pos);
    if (arr_start == std::string::npos) return filings;

    // Iterate through filing objects
    size_t pos = arr_start;
    while ((pos = response.find('{', pos)) != std::string::npos) {
        auto obj_end = response.find('}', pos);
        if (obj_end == std::string::npos) break;
        // Handle nested objects — find the matching closing brace
        int depth = 1;
        size_t scan = pos + 1;
        while (scan < response.size() && depth > 0) {
            if (response[scan] == '{') depth++;
            else if (response[scan] == '}') depth--;
            scan++;
        }
        obj_end = scan;
        std::string obj = response.substr(pos, obj_end - pos);

        // Field extractor
        auto get_str = [&](const std::string& key) -> std::string {
            auto p = obj.find("\"" + key + "\"");
            if (p == std::string::npos) return "";
            auto colon = obj.find(':', p);
            if (colon == std::string::npos) return "";
            auto q1 = obj.find('"', colon + 1);
            auto q2 = obj.find('"', q1 + 1);
            return (q1 != std::string::npos && q2 != std::string::npos)
                ? obj.substr(q1+1, q2-q1-1) : "";
        };

        Filing f;
        f.filing_id = get_str("id_filing");
        if (f.filing_id.empty()) f.filing_id = get_str("id");
        f.docket_number = get_str("proceedings.name");
        if (f.docket_number.empty()) f.docket_number = get_str("docket_number");
        f.proceeding_name = get_str("proceeding_name");
        f.filer_name = get_str("filers.name");
        if (f.filer_name.empty()) f.filer_name = get_str("filer_name");
        f.date_filed = get_str("date_submission");
        f.date_received = get_str("date_received");
        f.description = get_str("text_data");
        if (f.description.empty()) f.description = get_str("description");
        f.bureau = get_str("bureau.name");
        if (f.bureau.empty()) f.bureau = get_str("bureau");

        // Classify filing type from bureau and content
        // Reference: FCC Bureau list — https://www.fcc.gov/about/overview
        f.type = FilingType::ECFS_COMMENT;
        std::string type_str = get_str("type");
        if (type_str.find("reply") != std::string::npos) f.type = FilingType::ECFS_REPLY;
        else if (type_str.find("ex_parte") != std::string::npos) f.type = FilingType::ECFS_EX_PARTE;

        // Satellite and spectrum flags
        f.is_satellite_related = (f.bureau.find("Space") != std::string::npos ||
            f.bureau.find("International") != std::string::npos ||
            f.description.find("satellite") != std::string::npos ||
            f.description.find("NGSO") != std::string::npos);
        f.is_spectrum_related = (f.description.find("spectrum") != std::string::npos ||
            f.description.find("allocation") != std::string::npos ||
            f.description.find("MHz") != std::string::npos ||
            f.description.find("GHz") != std::string::npos);

        if (!f.filing_id.empty()) filings.push_back(f);
        pos = obj_end;
    }

    return filings;
}

std::vector<Filing> FCCClient::get_recent_filings(uint32_t hours, uint32_t limit) {
    ECFSQuery query;
    query.limit = limit;

    // Compute date_from as ISO-8601 date string (hours ago from epoch)
    // In production WASM, JavaScript Date provides real time.
    // In C++ native, use time() to compute the date offset.
    // Format: YYYY-MM-DD (ECFS API date filter format)
    {
        // Approximate: convert hours to days for date filter
        // ECFS API accepts dates in YYYY-MM-DD format
        // Reference: FCC ECFS API docs — date_received filter
        uint32_t days_back = (hours + 23) / 24;
        // We construct a relative date string — the JS bridge will resolve
        // epoch-relative dates. In native C++, use time() for real dates.
        query.date_from = "now-" + std::to_string(days_back) + "d";
    }

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
    // FCC ULS (Universal Licensing System) — Part 25 Satellite Licenses
    // Service codes: NN (Space Station), SA (Ship Radar), SE (Ship Earth Station)
    // Reference: https://www.fcc.gov/wireless/bureau-divisions/technologies-systems-and-innovation-division/rules-regulations-title-47/service-codes
    // Also: 47 CFR Part 25 — "Satellite Communications"
    std::string url = "https://publicapi.fcc.gov/uls/licenses?"
                      "serviceCode=NN&serviceCode=SA&serviceCode=SE";
    if (!status.empty()) url += "&status=" + status;
    if (!api_key_.empty()) url += "&api_key=" + api_key_;
    std::string response = http_get(url);

    std::vector<SatelliteLicense> licenses;

    // Parse ULS response JSON — "Licenses" array
    // Reference: FCC ULS API docs — /uls/licenses response schema
    auto lic_pos = response.find("\"Licenses\"");
    if (lic_pos == std::string::npos) lic_pos = response.find("\"licenses\"");
    if (lic_pos == std::string::npos) return licenses;

    size_t pos = lic_pos;
    while ((pos = response.find('{', pos + 1)) != std::string::npos) {
        // Find matching brace
        int depth = 1;
        size_t end = pos + 1;
        while (end < response.size() && depth > 0) {
            if (response[end] == '{') depth++;
            else if (response[end] == '}') depth--;
            end++;
        }
        std::string obj = response.substr(pos, end - pos);

        auto get = [&](const std::string& key) -> std::string {
            auto p = obj.find("\"" + key + "\"");
            if (p == std::string::npos) return "";
            auto q1 = obj.find('"', obj.find(':', p) + 1);
            auto q2 = obj.find('"', q1 + 1);
            return (q1 != std::string::npos && q2 != std::string::npos)
                ? obj.substr(q1+1, q2-q1-1) : "";
        };
        auto get_num = [&](const std::string& key) -> double {
            auto p = obj.find("\"" + key + "\"");
            if (p == std::string::npos) return 0;
            try { return std::stod(obj.substr(obj.find(':', p) + 1)); }
            catch (...) { return 0; }
        };

        SatelliteLicense l;
        l.call_sign = get("callSign");
        if (l.call_sign.empty()) l.call_sign = get("call_sign");
        l.licensee = get("licenseName");
        if (l.licensee.empty()) l.licensee = get("licensee");
        l.satellite_name = get("satellite_name");
        l.orbit_type = get("orbitType");
        l.orbital_altitude_km = get_num("orbital_altitude_km");
        l.orbital_longitude_deg = get_num("orbital_longitude_deg");
        l.inclination_deg = get_num("inclination_deg");
        l.num_satellites = static_cast<uint32_t>(get_num("num_satellites"));
        l.freq_lower_mhz = get_num("freq_lower_mhz");
        l.freq_upper_mhz = get_num("freq_upper_mhz");
        l.status = get("statusDesc");
        if (l.status.empty()) l.status = get("status");
        l.grant_date = get("grantDate");
        l.expiration_date = get("expiredDate");
        l.filing_id = get("filing_id");

        // Classify subtype from orbit and filing
        l.subtype = SatelliteFilingType::UNKNOWN;
        if (l.orbit_type == "NGSO") l.subtype = SatelliteFilingType::NGSO_CONSTELLATION;
        else if (l.orbit_type == "GSO") l.subtype = SatelliteFilingType::GSO_APPLICATION;

        if (!l.call_sign.empty()) licenses.push_back(l);
        pos = end;
    }

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

    // Parse spectrum allocation JSON response
    // Reference: FCC Spectrum Manager API — allocation records
    // Also: 47 CFR §2.106 — Table of Frequency Allocations
    //   https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-2/subpart-B
    std::vector<SpectrumAllocation> allocations;

    size_t pos = 0;
    while ((pos = response.find('{', pos)) != std::string::npos) {
        int depth = 1;
        size_t end = pos + 1;
        while (end < response.size() && depth > 0) {
            if (response[end] == '{') depth++;
            else if (response[end] == '}') depth--;
            end++;
        }
        std::string obj = response.substr(pos, end - pos);

        auto get = [&](const std::string& key) -> std::string {
            auto p = obj.find("\"" + key + "\"");
            if (p == std::string::npos) return "";
            auto q1 = obj.find('"', obj.find(':', p) + 1);
            auto q2 = obj.find('"', q1 + 1);
            return (q1 != std::string::npos && q2 != std::string::npos)
                ? obj.substr(q1+1, q2-q1-1) : "";
        };
        auto get_num = [&](const std::string& key) -> double {
            auto p = obj.find("\"" + key + "\"");
            if (p == std::string::npos) return 0;
            try { return std::stod(obj.substr(obj.find(':', p) + 1)); }
            catch (...) { return 0; }
        };

        SpectrumAllocation a;
        a.freq_lower_mhz = get_num("freqLower");
        if (a.freq_lower_mhz == 0) a.freq_lower_mhz = get_num("freq_lower_mhz");
        a.freq_upper_mhz = get_num("freqUpper");
        if (a.freq_upper_mhz == 0) a.freq_upper_mhz = get_num("freq_upper_mhz");
        a.allocation_type = get("allocationType");
        if (a.allocation_type.empty()) a.allocation_type = get("allocation_type");
        a.service = get("service");
        a.region = get("region");
        a.status = get("status");
        a.docket_number = get("docket_number");
        a.effective_date = get("effective_date");
        a.notes = get("notes");

        // Classify band per ITU Radio Regulations, Article 2
        // Reference: ITU-R Radio Regulations, Edition of 2020, Vol. 1, Art. 2
        double mid_freq = (a.freq_lower_mhz + a.freq_upper_mhz) / 2.0;
        if (mid_freq < 0.03) a.band = SpectrumBand::VLF;
        else if (mid_freq < 0.3) a.band = SpectrumBand::LF;
        else if (mid_freq < 3.0) a.band = SpectrumBand::MF;
        else if (mid_freq < 30.0) a.band = SpectrumBand::HF;
        else if (mid_freq < 300.0) a.band = SpectrumBand::VHF;
        else if (mid_freq < 3000.0) a.band = SpectrumBand::UHF;
        else if (mid_freq < 30000.0) a.band = SpectrumBand::SHF;
        else if (mid_freq < 300000.0) a.band = SpectrumBand::EHF;
        else a.band = SpectrumBand::UNKNOWN;

        if (a.freq_lower_mhz > 0 || a.freq_upper_mhz > 0) allocations.push_back(a);
        pos = end;
    }

    return allocations;
}

std::vector<SpectrumAllocation> FCCClient::get_proposed_reallocations() {
    // Look for NPRMs in spectrum-related proceedings
    // Reference: 47 CFR §2.106 — US Table of Frequency Allocations
    // NPRMs proposing allocation changes are filed under Wireless/OET bureaus.
    ECFSQuery query;
    query.type = FilingType::NPRM;
    query.spectrum_only = true;
    auto filings = query_filings(query);

    // Extract spectrum allocation proposals from NPRM filings
    std::vector<SpectrumAllocation> proposals;
    for (const auto& f : filings) {
        if (!f.is_spectrum_related) continue;

        SpectrumAllocation a;
        a.docket_number = f.docket_number;
        a.status = "proposed";
        a.effective_date = f.date_filed;
        a.notes = f.description;

        // Attempt to extract frequency range from description
        // Common patterns: "XXXX-YYYY MHz", "X.Y-Z.W GHz"
        auto extract_freq = [](const std::string& text, double& lower, double& upper) {
            // Search for MHz pattern
            auto mhz_pos = text.find("MHz");
            if (mhz_pos != std::string::npos) {
                // Walk backwards to find the range
                size_t dash = text.rfind('-', mhz_pos);
                if (dash != std::string::npos && dash > 0) {
                    try {
                        // Find start of first number
                        size_t num_start = dash;
                        while (num_start > 0 && (std::isdigit(text[num_start-1]) ||
                               text[num_start-1] == '.')) num_start--;
                        lower = std::stod(text.substr(num_start, dash - num_start));
                        upper = std::stod(text.substr(dash + 1));
                    } catch (...) {}
                }
            }
            // Search for GHz pattern (convert to MHz)
            auto ghz_pos = text.find("GHz");
            if (ghz_pos != std::string::npos && lower == 0) {
                size_t dash = text.rfind('-', ghz_pos);
                if (dash != std::string::npos && dash > 0) {
                    try {
                        size_t num_start = dash;
                        while (num_start > 0 && (std::isdigit(text[num_start-1]) ||
                               text[num_start-1] == '.')) num_start--;
                        lower = std::stod(text.substr(num_start, dash - num_start)) * 1000.0;
                        upper = std::stod(text.substr(dash + 1)) * 1000.0;
                    } catch (...) {}
                }
            }
        };

        extract_freq(f.description, a.freq_lower_mhz, a.freq_upper_mhz);

        // Classify band
        double mid = (a.freq_lower_mhz + a.freq_upper_mhz) / 2.0;
        if (mid < 300) a.band = SpectrumBand::VHF;
        else if (mid < 3000) a.band = SpectrumBand::UHF;
        else if (mid < 30000) a.band = SpectrumBand::SHF;
        else a.band = SpectrumBand::EHF;

        proposals.push_back(a);
    }

    return proposals;
}

std::vector<ULSRecord> FCCClient::search_uls(const std::string& query) {
    // FCC Universal Licensing System (ULS) API
    // Reference: https://www.fcc.gov/wireless/systems-utilities/universal-licensing-system
    // Response JSON: "Licenses" array with callSign, statusDesc, serviceDesc,
    //   licenseName, grantDate, expiredDate, frqLower, frqUpper, etc.
    std::string url = build_uls_url(query);
    std::string response = http_get(url);

    std::vector<ULSRecord> records;
    size_t pos = 0;
    while ((pos = response.find('{', pos + 1)) != std::string::npos) {
        int depth = 1;
        size_t end = pos + 1;
        while (end < response.size() && depth > 0) {
            if (response[end] == '{') depth++;
            else if (response[end] == '}') depth--;
            end++;
        }
        std::string obj = response.substr(pos, end - pos);

        auto get = [&](const std::string& key) -> std::string {
            auto p = obj.find("\"" + key + "\"");
            if (p == std::string::npos) return "";
            auto q1 = obj.find('"', obj.find(':', p) + 1);
            auto q2 = obj.find('"', q1 + 1);
            return (q1 != std::string::npos && q2 != std::string::npos)
                ? obj.substr(q1+1, q2-q1-1) : "";
        };
        auto get_num = [&](const std::string& key) -> double {
            auto p = obj.find("\"" + key + "\"");
            if (p == std::string::npos) return 0;
            try { return std::stod(obj.substr(obj.find(':', p) + 1)); }
            catch (...) { return 0; }
        };

        ULSRecord r;
        r.call_sign = get("callSign");
        if (r.call_sign.empty()) continue;
        r.licensee = get("licenseName");
        r.service_code = get("serviceCode");
        r.status = get("statusDesc");
        r.freq_lower_mhz = get_num("frqLower");
        r.freq_upper_mhz = get_num("frqUpper");
        r.power_watts = get_num("power_watts");
        r.latitude = get_num("latitude");
        r.longitude = get_num("longitude");
        r.grant_date = get("grantDate");
        r.expiration_date = get("expiredDate");
        r.market = get("marketDesc");

        records.push_back(r);
        pos = end;
    }

    return records;
}

std::vector<ULSRecord> FCCClient::get_uls_by_callsign(const std::string& call_sign) {
    return search_uls("callSign=" + call_sign);
}

std::vector<ULSRecord> FCCClient::get_uls_by_licensee(const std::string& licensee) {
    return search_uls("licensee=" + licensee);
}

std::vector<ExperimentalLicense> FCCClient::get_experimental_licenses(uint32_t hours) {
    // FCC OET Experimental Licensing System (ELS)
    // Reference: https://apps.fcc.gov/oetcf/els/reports/GenericSearch.cfm
    // Export endpoint: /442_Export.cfm?format=json
    // Fields: callSign, grantee, expType, purpose, freqLow, freqHigh,
    //   power, lat, lon, radius, grantDate, expirationDate, status
    // Reference: 47 CFR Part 5 — "Experimental Radio Service"
    std::string url = "https://apps.fcc.gov/oetcf/els/reports/442_Export.cfm?"
                      "format=json";
    std::string response = http_get(url);

    std::vector<ExperimentalLicense> licenses;
    size_t pos = 0;
    while ((pos = response.find('{', pos + 1)) != std::string::npos) {
        int depth = 1;
        size_t end = pos + 1;
        while (end < response.size() && depth > 0) {
            if (response[end] == '{') depth++;
            else if (response[end] == '}') depth--;
            end++;
        }
        std::string obj = response.substr(pos, end - pos);

        auto get = [&](const std::string& key) -> std::string {
            auto p = obj.find("\"" + key + "\"");
            if (p == std::string::npos) return "";
            auto q1 = obj.find('"', obj.find(':', p) + 1);
            auto q2 = obj.find('"', q1 + 1);
            return (q1 != std::string::npos && q2 != std::string::npos)
                ? obj.substr(q1+1, q2-q1-1) : "";
        };
        auto get_num = [&](const std::string& key) -> double {
            auto p = obj.find("\"" + key + "\"");
            if (p == std::string::npos) return 0;
            try { return std::stod(obj.substr(obj.find(':', p) + 1)); }
            catch (...) { return 0; }
        };

        ExperimentalLicense l;
        l.call_sign = get("callSign");
        if (l.call_sign.empty()) continue;
        l.licensee = get("grantee");
        l.experiment_type = get("expType");
        l.purpose = get("purpose");
        l.freq_lower_mhz = get_num("freqLow");
        l.freq_upper_mhz = get_num("freqHigh");
        l.power_watts = get_num("power");
        l.latitude = get_num("lat");
        l.longitude = get_num("lon");
        l.radius_km = get_num("radius");
        l.grant_date = get("grantDate");
        l.expiration_date = get("expirationDate");
        l.status = get("status");

        // Detect space-related experiments from purpose/type
        l.is_space_related = (l.purpose.find("satellite") != std::string::npos ||
            l.purpose.find("space") != std::string::npos ||
            l.purpose.find("orbital") != std::string::npos ||
            l.purpose.find("launch") != std::string::npos ||
            l.freq_upper_mhz > 10000);  // Ka/V band often space-related

        licenses.push_back(l);
        pos = end;
    }

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
    // FCC Equipment Authorization System (EAS)
    // Reference: https://www.fcc.gov/oet/ea/fccid
    // Search API: https://publicapi.fcc.gov/equipment/search
    // Fields: fccId, granteeName, productDescription, equipmentClass,
    //   lowerFreq, upperFreq, powerOutput, grantDate, testFirm
    // Reference: 47 CFR Part 2, Subpart J — "Equipment Authorization Procedures"
    std::string url = "https://publicapi.fcc.gov/equipment/search?query=" + query;
    if (!api_key_.empty()) url += "&api_key=" + api_key_;
    std::string response = http_get(url);

    std::vector<EquipmentAuth> auths;
    size_t pos = 0;
    while ((pos = response.find('{', pos + 1)) != std::string::npos) {
        int depth = 1;
        size_t end = pos + 1;
        while (end < response.size() && depth > 0) {
            if (response[end] == '{') depth++;
            else if (response[end] == '}') depth--;
            end++;
        }
        std::string obj = response.substr(pos, end - pos);

        auto get = [&](const std::string& key) -> std::string {
            auto p = obj.find("\"" + key + "\"");
            if (p == std::string::npos) return "";
            auto q1 = obj.find('"', obj.find(':', p) + 1);
            auto q2 = obj.find('"', q1 + 1);
            return (q1 != std::string::npos && q2 != std::string::npos)
                ? obj.substr(q1+1, q2-q1-1) : "";
        };
        auto get_num = [&](const std::string& key) -> double {
            auto p = obj.find("\"" + key + "\"");
            if (p == std::string::npos) return 0;
            try { return std::stod(obj.substr(obj.find(':', p) + 1)); }
            catch (...) { return 0; }
        };

        EquipmentAuth a;
        a.fcc_id = get("fccId");
        if (a.fcc_id.empty()) continue;
        a.grantee_name = get("granteeName");
        a.product_description = get("productDescription");
        a.equipment_class = get("equipmentClass");
        a.freq_lower_mhz = get_num("lowerFreq");
        a.freq_upper_mhz = get_num("upperFreq");
        a.power_output_watts = get_num("powerOutput");
        a.grant_date = get("grantDate");
        a.test_firm = get("testFirm");

        // SDR detection: equipment class "DSS" or description mentions SDR
        a.is_sdr = (a.equipment_class == "DSS" ||
            a.product_description.find("SDR") != std::string::npos ||
            a.product_description.find("Software Defined") != std::string::npos);

        // Satellite terminal detection
        a.is_satellite_terminal = (
            a.product_description.find("satellite") != std::string::npos ||
            a.product_description.find("VSAT") != std::string::npos ||
            a.product_description.find("earth station") != std::string::npos ||
            a.product_description.find("Starlink") != std::string::npos);

        auths.push_back(a);
        pos = end;
    }

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
    // HTTP bridge — resolved at link time.
    // WASM: JavaScript bridge provides fetch via emscripten_fetch or imported function.
    // Native: link a real HTTP client (curl, etc.) for integration tests.
    // Reference: Emscripten Fetch API documentation
    (void)url;
    return "{}";
}

// ── JSON Record Parsers ─────────────────────────────────────────────────────
// Each parser extracts a single record from a JSON string.
// Used for by-ID lookups that return a single object.
// Reference: FCC Public API documentation — https://www.fcc.gov/developers

// Helper: extract a string field from a JSON object
static std::string json_str(const std::string& json, const std::string& key) {
    auto p = json.find("\"" + key + "\"");
    if (p == std::string::npos) return "";
    auto q1 = json.find('"', json.find(':', p) + 1);
    auto q2 = json.find('"', q1 + 1);
    return (q1 != std::string::npos && q2 != std::string::npos)
        ? json.substr(q1+1, q2-q1-1) : "";
}

static double json_num(const std::string& json, const std::string& key) {
    auto p = json.find("\"" + key + "\"");
    if (p == std::string::npos) return 0;
    try { return std::stod(json.substr(json.find(':', p) + 1)); }
    catch (...) { return 0; }
}

Filing FCCClient::parse_ecfs_filing(const std::string& json_record) {
    // Parse a single ECFS filing JSON record.
    // Reference: FCC ECFS API — /ecfs/filings/{id} response schema
    Filing f;
    f.filing_id = json_str(json_record, "id_filing");
    if (f.filing_id.empty()) f.filing_id = json_str(json_record, "id");
    f.docket_number = json_str(json_record, "proceedings.name");
    if (f.docket_number.empty()) f.docket_number = json_str(json_record, "docket_number");
    f.proceeding_name = json_str(json_record, "proceeding_name");
    f.filer_name = json_str(json_record, "filers.name");
    if (f.filer_name.empty()) f.filer_name = json_str(json_record, "filer_name");
    f.date_filed = json_str(json_record, "date_submission");
    f.date_received = json_str(json_record, "date_received");
    f.description = json_str(json_record, "text_data");
    if (f.description.empty()) f.description = json_str(json_record, "description");
    f.bureau = json_str(json_record, "bureau.name");
    if (f.bureau.empty()) f.bureau = json_str(json_record, "bureau");

    // Filing type classification
    std::string type_str = json_str(json_record, "type");
    f.type = FilingType::ECFS_COMMENT;
    if (type_str.find("reply") != std::string::npos) f.type = FilingType::ECFS_REPLY;
    else if (type_str.find("ex_parte") != std::string::npos) f.type = FilingType::ECFS_EX_PARTE;
    else if (type_str.find("NPRM") != std::string::npos) f.type = FilingType::NPRM;
    else if (type_str.find("ORDER") != std::string::npos) f.type = FilingType::ORDER;

    f.is_satellite_related = (f.bureau.find("Space") != std::string::npos ||
        f.description.find("satellite") != std::string::npos);
    f.is_spectrum_related = (f.description.find("spectrum") != std::string::npos ||
        f.description.find("MHz") != std::string::npos);

    return f;
}

SatelliteLicense FCCClient::parse_satellite_license(const std::string& json_record) {
    // Parse a single ULS satellite license record.
    // Reference: FCC ULS API — /uls/licenses response schema
    SatelliteLicense l;
    l.call_sign = json_str(json_record, "callSign");
    if (l.call_sign.empty()) l.call_sign = json_str(json_record, "call_sign");
    l.licensee = json_str(json_record, "licenseName");
    if (l.licensee.empty()) l.licensee = json_str(json_record, "licensee");
    l.satellite_name = json_str(json_record, "satellite_name");
    l.orbit_type = json_str(json_record, "orbitType");
    l.orbital_altitude_km = json_num(json_record, "orbital_altitude_km");
    l.orbital_longitude_deg = json_num(json_record, "orbital_longitude_deg");
    l.inclination_deg = json_num(json_record, "inclination_deg");
    l.num_satellites = static_cast<uint32_t>(json_num(json_record, "num_satellites"));
    l.freq_lower_mhz = json_num(json_record, "freq_lower_mhz");
    l.freq_upper_mhz = json_num(json_record, "freq_upper_mhz");
    l.status = json_str(json_record, "statusDesc");
    if (l.status.empty()) l.status = json_str(json_record, "status");
    l.grant_date = json_str(json_record, "grantDate");
    l.expiration_date = json_str(json_record, "expiredDate");

    l.subtype = SatelliteFilingType::UNKNOWN;
    if (l.orbit_type == "NGSO") l.subtype = SatelliteFilingType::NGSO_CONSTELLATION;
    else if (l.orbit_type == "GSO") l.subtype = SatelliteFilingType::GSO_APPLICATION;

    return l;
}

SpectrumAllocation FCCClient::parse_allocation(const std::string& json_record) {
    // Parse a single spectrum allocation record.
    // Reference: 47 CFR §2.106 — Table of Frequency Allocations
    SpectrumAllocation a;
    a.freq_lower_mhz = json_num(json_record, "freqLower");
    if (a.freq_lower_mhz == 0) a.freq_lower_mhz = json_num(json_record, "freq_lower_mhz");
    a.freq_upper_mhz = json_num(json_record, "freqUpper");
    if (a.freq_upper_mhz == 0) a.freq_upper_mhz = json_num(json_record, "freq_upper_mhz");
    a.allocation_type = json_str(json_record, "allocationType");
    a.service = json_str(json_record, "service");
    a.region = json_str(json_record, "region");
    a.status = json_str(json_record, "status");
    a.notes = json_str(json_record, "notes");

    // Band classification — ITU Radio Regulations, Article 2
    double mid = (a.freq_lower_mhz + a.freq_upper_mhz) / 2.0;
    if (mid < 0.03) a.band = SpectrumBand::VLF;
    else if (mid < 0.3) a.band = SpectrumBand::LF;
    else if (mid < 3.0) a.band = SpectrumBand::MF;
    else if (mid < 30.0) a.band = SpectrumBand::HF;
    else if (mid < 300.0) a.band = SpectrumBand::VHF;
    else if (mid < 3000.0) a.band = SpectrumBand::UHF;
    else if (mid < 30000.0) a.band = SpectrumBand::SHF;
    else if (mid < 300000.0) a.band = SpectrumBand::EHF;
    else a.band = SpectrumBand::UNKNOWN;

    return a;
}

ULSRecord FCCClient::parse_uls_record(const std::string& json_record) {
    // Parse a single ULS record.
    // Reference: FCC ULS API response schema
    ULSRecord r;
    r.call_sign = json_str(json_record, "callSign");
    r.licensee = json_str(json_record, "licenseName");
    r.service_code = json_str(json_record, "serviceCode");
    r.status = json_str(json_record, "statusDesc");
    r.freq_lower_mhz = json_num(json_record, "frqLower");
    r.freq_upper_mhz = json_num(json_record, "frqUpper");
    r.power_watts = json_num(json_record, "power_watts");
    r.latitude = json_num(json_record, "latitude");
    r.longitude = json_num(json_record, "longitude");
    r.grant_date = json_str(json_record, "grantDate");
    r.expiration_date = json_str(json_record, "expiredDate");
    r.market = json_str(json_record, "marketDesc");
    return r;
}

ExperimentalLicense FCCClient::parse_experimental(const std::string& json_record) {
    // Parse a single experimental license record.
    // Reference: 47 CFR Part 5 — Experimental Radio Service
    ExperimentalLicense l;
    l.call_sign = json_str(json_record, "callSign");
    l.licensee = json_str(json_record, "grantee");
    l.experiment_type = json_str(json_record, "expType");
    l.purpose = json_str(json_record, "purpose");
    l.freq_lower_mhz = json_num(json_record, "freqLow");
    l.freq_upper_mhz = json_num(json_record, "freqHigh");
    l.power_watts = json_num(json_record, "power");
    l.latitude = json_num(json_record, "lat");
    l.longitude = json_num(json_record, "lon");
    l.radius_km = json_num(json_record, "radius");
    l.grant_date = json_str(json_record, "grantDate");
    l.expiration_date = json_str(json_record, "expirationDate");
    l.status = json_str(json_record, "status");
    l.is_space_related = (l.purpose.find("satellite") != std::string::npos ||
        l.purpose.find("space") != std::string::npos);
    return l;
}

EquipmentAuth FCCClient::parse_equipment(const std::string& json_record) {
    // Parse a single equipment authorization record.
    // Reference: 47 CFR Part 2, Subpart J — Equipment Authorization Procedures
    EquipmentAuth a;
    a.fcc_id = json_str(json_record, "fccId");
    a.grantee_name = json_str(json_record, "granteeName");
    a.product_description = json_str(json_record, "productDescription");
    a.equipment_class = json_str(json_record, "equipmentClass");
    a.freq_lower_mhz = json_num(json_record, "lowerFreq");
    a.freq_upper_mhz = json_num(json_record, "upperFreq");
    a.power_output_watts = json_num(json_record, "powerOutput");
    a.grant_date = json_str(json_record, "grantDate");
    a.test_firm = json_str(json_record, "testFirm");
    a.is_sdr = (a.equipment_class == "DSS");
    a.is_satellite_terminal = (a.product_description.find("satellite") != std::string::npos ||
        a.product_description.find("VSAT") != std::string::npos);
    return a;
}

}  // namespace fcc
