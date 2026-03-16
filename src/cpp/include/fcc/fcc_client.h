#pragma once

#include "fcc/types.h"
#include <string>
#include <vector>
#include <functional>

namespace fcc {

/**
 * FCC API Client
 *
 * Interfaces with multiple FCC data systems:
 * - ECFS (Electronic Comment Filing System) — filings, comments, ex parte notices
 * - ULS (Universal Licensing System) — spectrum licenses, earth stations, satellite authorizations
 * - Equipment Authorization — FCC IDs, equipment certifications
 * - Experimental Licensing — Part 5 experimental authorizations
 * - Spectrum Dashboard — allocation table, auction results
 *
 * Data sources:
 *   https://publicapi.fcc.gov/ecfs/filings
 *   https://publicapi.fcc.gov/ecfs/proceedings
 *   https://auctiondata.fcc.gov/
 *   https://apps.fcc.gov/oetcf/eas/reports/GenericSearch.cfm
 *   https://wireless2.fcc.gov/UlsApp/UlsSearch/searchLicense.jsp
 *   https://transition.fcc.gov/oet/ea/fccid/
 *   https://apps.fcc.gov/els/GetLicenseByFRN.html
 */
class FCCClient {
public:
    FCCClient();
    ~FCCClient();

    // ECFS Filing queries
    std::vector<Filing> query_filings(const ECFSQuery& query);
    std::vector<Filing> get_recent_filings(uint32_t hours = 24, uint32_t limit = 100);
    std::vector<Filing> get_satellite_filings(uint32_t hours = 24);
    Filing get_filing_by_id(const std::string& filing_id);

    // Proceeding/Docket queries
    std::vector<Filing> get_docket_filings(const std::string& docket_number);

    // Satellite license queries
    std::vector<SatelliteLicense> get_satellite_licenses(const std::string& status = "");
    std::vector<SatelliteLicense> get_ngso_constellations();
    SatelliteLicense get_license_by_callsign(const std::string& call_sign);

    // Spectrum allocation queries
    std::vector<SpectrumAllocation> get_allocations(double freq_lower_mhz = 0,
                                                      double freq_upper_mhz = 300000);
    std::vector<SpectrumAllocation> get_proposed_reallocations();

    // ULS queries
    std::vector<ULSRecord> search_uls(const std::string& query);
    std::vector<ULSRecord> get_uls_by_callsign(const std::string& call_sign);
    std::vector<ULSRecord> get_uls_by_licensee(const std::string& licensee);

    // Experimental license queries
    std::vector<ExperimentalLicense> get_experimental_licenses(uint32_t hours = 168);
    std::vector<ExperimentalLicense> get_space_experimental_licenses();

    // Equipment authorization queries
    std::vector<EquipmentAuth> search_equipment(const std::string& query);
    EquipmentAuth get_equipment_by_fccid(const std::string& fcc_id);
    std::vector<EquipmentAuth> get_satellite_terminals();

    // Alert generation
    std::vector<FilingAlert> check_for_alerts(uint32_t hours = 24);

    // Polling support
    void set_poll_interval_seconds(uint32_t seconds);
    void set_api_key(const std::string& key);

private:
    std::string api_key_;
    uint32_t poll_interval_seconds_;
    std::string last_poll_timestamp_;

    // HTTP helpers (WASM-compatible via emscripten_fetch or JS bridge)
    std::string http_get(const std::string& url);
    std::string build_ecfs_url(const ECFSQuery& query);
    std::string build_uls_url(const std::string& query);

    // Parsing helpers
    Filing parse_ecfs_filing(const std::string& json_record);
    SatelliteLicense parse_satellite_license(const std::string& json_record);
    SpectrumAllocation parse_allocation(const std::string& json_record);
    ULSRecord parse_uls_record(const std::string& json_record);
    ExperimentalLicense parse_experimental(const std::string& json_record);
    EquipmentAuth parse_equipment(const std::string& json_record);

    // Alert classification
    FilingAlert classify_filing(const Filing& filing);
    bool is_significant_filing(const Filing& filing);
    std::string determine_severity(const Filing& filing);
};

}  // namespace fcc
