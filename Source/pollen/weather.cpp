#include "pollen/weather.hpp"

#include <fizmo/../networking.hpp>

#include <cctype>
#include <cstdio>
#include <future>

namespace pollen {

namespace http = fizmo::networking::http;

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    std::size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    return s.substr(a);
}

std::string number(double v) {
    return coordinate_text(v);
}

struct Region {
    const char* abbreviation;
    const char* name;
    const char* country;
};

const Region regions[] = {
    { "al", "alabama", "us" }, { "ak", "alaska", "us" }, { "az", "arizona", "us" }, { "ar", "arkansas", "us" }, { "ca", "california", "us" },
    { "co", "colorado", "us" }, { "ct", "connecticut", "us" }, { "de", "delaware", "us" }, { "dc", "district of columbia", "us" }, { "fl", "florida", "us" },
    { "ga", "georgia", "us" }, { "hi", "hawaii", "us" }, { "id", "idaho", "us" }, { "il", "illinois", "us" }, { "in", "indiana", "us" },
    { "ia", "iowa", "us" }, { "ks", "kansas", "us" }, { "ky", "kentucky", "us" }, { "la", "louisiana", "us" }, { "me", "maine", "us" },
    { "md", "maryland", "us" }, { "ma", "massachusetts", "us" }, { "mi", "michigan", "us" }, { "mn", "minnesota", "us" }, { "ms", "mississippi", "us" },
    { "mo", "missouri", "us" }, { "mt", "montana", "us" }, { "ne", "nebraska", "us" }, { "nv", "nevada", "us" }, { "nh", "new hampshire", "us" },
    { "nj", "new jersey", "us" }, { "nm", "new mexico", "us" }, { "ny", "new york", "us" }, { "nc", "north carolina", "us" }, { "nd", "north dakota", "us" },
    { "oh", "ohio", "us" }, { "ok", "oklahoma", "us" }, { "or", "oregon", "us" }, { "pa", "pennsylvania", "us" }, { "ri", "rhode island", "us" },
    { "sc", "south carolina", "us" }, { "sd", "south dakota", "us" }, { "tn", "tennessee", "us" }, { "tx", "texas", "us" }, { "ut", "utah", "us" },
    { "vt", "vermont", "us" }, { "va", "virginia", "us" }, { "wa", "washington", "us" }, { "wv", "west virginia", "us" }, { "wi", "wisconsin", "us" },
    { "wy", "wyoming", "us" }, { "pr", "puerto rico", "pr" },
    { "ab", "alberta", "ca" }, { "bc", "british columbia", "ca" }, { "mb", "manitoba", "ca" }, { "nb", "new brunswick", "ca" }, { "nl", "newfoundland and labrador", "ca" },
    { "ns", "nova scotia", "ca" }, { "nt", "northwest territories", "ca" }, { "nu", "nunavut", "ca" }, { "on", "ontario", "ca" }, { "pe", "prince edward island", "ca" },
    { "qc", "quebec", "ca" }, { "sk", "saskatchewan", "ca" }, { "yt", "yukon", "ca" },
    { "nsw", "new south wales", "au" }, { "vic", "victoria", "au" }, { "qld", "queensland", "au" }, { "wa", "western australia", "au" }, { "sa", "south australia", "au" },
    { "tas", "tasmania", "au" }, { "act", "australian capital territory", "au" }, { "nt", "northern territory", "au" },
};

struct CountryAlias {
    const char* alias;
    const char* code;
};

const CountryAlias country_aliases[] = {
    { "usa", "us" }, { "us", "us" }, { "u.s.", "us" }, { "u.s.a.", "us" }, { "america", "us" }, { "united states", "us" }, { "united states of america", "us" },
    { "uk", "gb" }, { "u.k.", "gb" }, { "gb", "gb" }, { "britain", "gb" }, { "great britain", "gb" }, { "united kingdom", "gb" },
    { "england", "gb" }, { "scotland", "gb" }, { "wales", "gb" }, { "northern ireland", "gb" },
    { "uae", "ae" }, { "emirates", "ae" }, { "south korea", "kr" }, { "korea", "kr" }, { "north korea", "kp" },
    { "czechia", "cz" }, { "czech republic", "cz" }, { "holland", "nl" }, { "the netherlands", "nl" }, { "netherlands", "nl" },
    { "russia", "ru" }, { "ivory coast", "ci" }, { "vatican", "va" }, { "nz", "nz" }, { "aus", "au" }, { "oz", "au" },
    { "deutschland", "de" }, { "espana", "es" }, { "españa", "es" }, { "nippon", "jp" }, { "turkiye", "tr" }, { "türkiye", "tr" },
};

const CountryAlias country_names[] = {
    { "afghanistan", "af" }, { "aland islands", "ax" }, { "albania", "al" }, { "algeria", "dz" }, { "american samoa", "as" }, { "andorra", "ad" }, { "angola", "ao" },
    { "anguilla", "ai" }, { "antarctica", "aq" }, { "antigua and barbuda", "ag" }, { "arab republic of egypt", "eg" }, { "argentina", "ar" }, { "argentine republic", "ar" },
    { "armenia", "am" }, { "aruba", "aw" }, { "australia", "au" }, { "austria", "at" }, { "azerbaijan", "az" }, { "bahamas", "bs" }, { "bahrain", "bh" }, { "bangladesh", "bd" },
    { "barbados", "bb" }, { "belarus", "by" }, { "belgium", "be" }, { "belize", "bz" }, { "benin", "bj" }, { "bermuda", "bm" }, { "bhutan", "bt" },
    { "bolivarian republic of venezuela", "ve" }, { "bolivia", "bo" }, { "bosnia and herzegovina", "ba" }, { "botswana", "bw" }, { "bouvet island", "bv" }, { "brazil", "br" },
    { "british indian ocean territory", "io" }, { "british virgin islands", "vg" }, { "brunei darussalam", "bn" }, { "bulgaria", "bg" }, { "burkina faso", "bf" },
    { "burundi", "bi" }, { "cabo verde", "cv" }, { "cambodia", "kh" }, { "cameroon", "cm" }, { "canada", "ca" }, { "cayman islands", "ky" }, { "central african republic", "cf" },
    { "chad", "td" }, { "chile", "cl" }, { "china", "cn" }, { "christmas island", "cx" }, { "cocos (keeling) islands", "cc" }, { "colombia", "co" },
    { "commonwealth of dominica", "dm" }, { "commonwealth of the bahamas", "bs" }, { "commonwealth of the northern mariana islands", "mp" }, { "comoros", "km" }, { "congo", "cg" },
    { "cook islands", "ck" }, { "costa rica", "cr" }, { "cote d'ivoire", "ci" }, { "croatia", "hr" }, { "cuba", "cu" }, { "curacao", "cw" }, { "curaçao", "cw" },
    { "cyprus", "cy" }, { "czech republic", "cz" }, { "czechia", "cz" }, { "côte d'ivoire", "ci" }, { "democratic people's republic of korea", "kp" },
    { "democratic republic of sao tome and principe", "st" }, { "democratic republic of timor-leste", "tl" }, { "democratic socialist republic of sri lanka", "lk" },
    { "denmark", "dk" }, { "djibouti", "dj" }, { "dominica", "dm" }, { "dominican republic", "do" }, { "eastern republic of uruguay", "uy" }, { "ecuador", "ec" },
    { "egypt", "eg" }, { "el salvador", "sv" }, { "equatorial guinea", "gq" }, { "eritrea", "er" }, { "estonia", "ee" }, { "eswatini", "sz" }, { "ethiopia", "et" },
    { "falkland islands (malvinas)", "fk" }, { "faroe islands", "fo" }, { "federal democratic republic of ethiopia", "et" }, { "federal democratic republic of nepal", "np" },
    { "federal republic of germany", "de" }, { "federal republic of nigeria", "ng" }, { "federal republic of somalia", "so" }, { "federated states of micronesia", "fm" },
    { "federative republic of brazil", "br" }, { "fiji", "fj" }, { "finland", "fi" }, { "france", "fr" }, { "french guiana", "gf" }, { "french polynesia", "pf" },
    { "french republic", "fr" }, { "french southern territories", "tf" }, { "gabon", "ga" }, { "gabonese republic", "ga" }, { "gambia", "gm" }, { "georgia", "ge" },
    { "germany", "de" }, { "ghana", "gh" }, { "gibraltar", "gi" }, { "grand duchy of luxembourg", "lu" }, { "greece", "gr" }, { "greenland", "gl" }, { "grenada", "gd" },
    { "guadeloupe", "gp" }, { "guam", "gu" }, { "guatemala", "gt" }, { "guernsey", "gg" }, { "guinea", "gn" }, { "guinea-bissau", "gw" }, { "guyana", "gy" }, { "haiti", "ht" },
    { "hashemite kingdom of jordan", "jo" }, { "heard island and mcdonald islands", "hm" }, { "hellenic republic", "gr" }, { "holy see (vatican city state)", "va" },
    { "honduras", "hn" }, { "hong kong", "hk" }, { "hong kong special administrative region of china", "hk" }, { "hungary", "hu" }, { "iceland", "is" },
    { "independent state of papua new guinea", "pg" }, { "independent state of samoa", "ws" }, { "india", "in" }, { "indonesia", "id" }, { "iran", "ir" }, { "iraq", "iq" },
    { "ireland", "ie" }, { "islamic republic of afghanistan", "af" }, { "islamic republic of iran", "ir" }, { "islamic republic of mauritania", "mr" },
    { "islamic republic of pakistan", "pk" }, { "isle of man", "im" }, { "israel", "il" }, { "italian republic", "it" }, { "italy", "it" }, { "jamaica", "jm" }, { "japan", "jp" },
    { "jersey", "je" }, { "jordan", "jo" }, { "kazakhstan", "kz" }, { "kenya", "ke" }, { "kingdom of bahrain", "bh" }, { "kingdom of belgium", "be" },
    { "kingdom of bhutan", "bt" }, { "kingdom of cambodia", "kh" }, { "kingdom of denmark", "dk" }, { "kingdom of eswatini", "sz" }, { "kingdom of lesotho", "ls" },
    { "kingdom of morocco", "ma" }, { "kingdom of norway", "no" }, { "kingdom of saudi arabia", "sa" }, { "kingdom of spain", "es" }, { "kingdom of sweden", "se" },
    { "kingdom of thailand", "th" }, { "kingdom of the netherlands", "nl" }, { "kingdom of tonga", "to" }, { "kiribati", "ki" }, { "kuwait", "kw" }, { "kyrgyz republic", "kg" },
    { "kyrgyzstan", "kg" }, { "lao people's democratic republic", "la" }, { "laos", "la" }, { "latvia", "lv" }, { "lebanese republic", "lb" }, { "lebanon", "lb" },
    { "lesotho", "ls" }, { "liberia", "lr" }, { "libya", "ly" }, { "liechtenstein", "li" }, { "lithuania", "lt" }, { "luxembourg", "lu" }, { "macao", "mo" },
    { "macao special administrative region of china", "mo" }, { "madagascar", "mg" }, { "malawi", "mw" }, { "malaysia", "my" }, { "maldives", "mv" }, { "mali", "ml" },
    { "malta", "mt" }, { "marshall islands", "mh" }, { "martinique", "mq" }, { "mauritania", "mr" }, { "mauritius", "mu" }, { "mayotte", "yt" }, { "mexico", "mx" },
    { "moldova", "md" }, { "monaco", "mc" }, { "mongolia", "mn" }, { "montenegro", "me" }, { "montserrat", "ms" }, { "morocco", "ma" }, { "mozambique", "mz" }, { "myanmar", "mm" },
    { "namibia", "na" }, { "nauru", "nr" }, { "nepal", "np" }, { "netherlands", "nl" }, { "new caledonia", "nc" }, { "new zealand", "nz" }, { "nicaragua", "ni" },
    { "niger", "ne" }, { "nigeria", "ng" }, { "niue", "nu" }, { "norfolk island", "nf" }, { "north korea", "kp" }, { "north macedonia", "mk" },
    { "northern mariana islands", "mp" }, { "norway", "no" }, { "oman", "om" }, { "pakistan", "pk" }, { "palau", "pw" }, { "panama", "pa" }, { "papua new guinea", "pg" },
    { "paraguay", "py" }, { "people's democratic republic of algeria", "dz" }, { "people's republic of bangladesh", "bd" }, { "people's republic of china", "cn" },
    { "peru", "pe" }, { "philippines", "ph" }, { "pitcairn", "pn" }, { "plurinational state of bolivia", "bo" }, { "poland", "pl" }, { "portugal", "pt" },
    { "portuguese republic", "pt" }, { "principality of andorra", "ad" }, { "principality of liechtenstein", "li" }, { "principality of monaco", "mc" }, { "puerto rico", "pr" },
    { "qatar", "qa" }, { "republic of albania", "al" }, { "republic of angola", "ao" }, { "republic of armenia", "am" }, { "republic of austria", "at" },
    { "republic of azerbaijan", "az" }, { "republic of belarus", "by" }, { "republic of benin", "bj" }, { "republic of bosnia and herzegovina", "ba" },
    { "republic of botswana", "bw" }, { "republic of bulgaria", "bg" }, { "republic of burundi", "bi" }, { "republic of cabo verde", "cv" }, { "republic of cameroon", "cm" },
    { "republic of chad", "td" }, { "republic of chile", "cl" }, { "republic of colombia", "co" }, { "republic of costa rica", "cr" }, { "republic of cote d'ivoire", "ci" },
    { "republic of croatia", "hr" }, { "republic of cuba", "cu" }, { "republic of cyprus", "cy" }, { "republic of côte d'ivoire", "ci" }, { "republic of djibouti", "dj" },
    { "republic of ecuador", "ec" }, { "republic of el salvador", "sv" }, { "republic of equatorial guinea", "gq" }, { "republic of estonia", "ee" }, { "republic of fiji", "fj" },
    { "republic of finland", "fi" }, { "republic of ghana", "gh" }, { "republic of guatemala", "gt" }, { "republic of guinea", "gn" }, { "republic of guinea-bissau", "gw" },
    { "republic of guyana", "gy" }, { "republic of haiti", "ht" }, { "republic of honduras", "hn" }, { "republic of iceland", "is" }, { "republic of india", "in" },
    { "republic of indonesia", "id" }, { "republic of iraq", "iq" }, { "republic of kazakhstan", "kz" }, { "republic of kenya", "ke" }, { "republic of kiribati", "ki" },
    { "republic of latvia", "lv" }, { "republic of liberia", "lr" }, { "republic of lithuania", "lt" }, { "republic of madagascar", "mg" }, { "republic of malawi", "mw" },
    { "republic of maldives", "mv" }, { "republic of mali", "ml" }, { "republic of malta", "mt" }, { "republic of mauritius", "mu" }, { "republic of moldova", "md" },
    { "republic of mozambique", "mz" }, { "republic of myanmar", "mm" }, { "republic of namibia", "na" }, { "republic of nauru", "nr" }, { "republic of nicaragua", "ni" },
    { "republic of north macedonia", "mk" }, { "republic of palau", "pw" }, { "republic of panama", "pa" }, { "republic of paraguay", "py" }, { "republic of peru", "pe" },
    { "republic of poland", "pl" }, { "republic of san marino", "sm" }, { "republic of senegal", "sn" }, { "republic of serbia", "rs" }, { "republic of seychelles", "sc" },
    { "republic of sierra leone", "sl" }, { "republic of singapore", "sg" }, { "republic of slovenia", "si" }, { "republic of south africa", "za" },
    { "republic of south sudan", "ss" }, { "republic of suriname", "sr" }, { "republic of tajikistan", "tj" }, { "republic of the congo", "cg" },
    { "republic of the gambia", "gm" }, { "republic of the marshall islands", "mh" }, { "republic of the niger", "ne" }, { "republic of the philippines", "ph" },
    { "republic of the sudan", "sd" }, { "republic of trinidad and tobago", "tt" }, { "republic of tunisia", "tn" }, { "republic of turkiye", "tr" },
    { "republic of türkiye", "tr" }, { "republic of uganda", "ug" }, { "republic of uzbekistan", "uz" }, { "republic of vanuatu", "vu" }, { "republic of yemen", "ye" },
    { "republic of zambia", "zm" }, { "republic of zimbabwe", "zw" }, { "reunion", "re" }, { "romania", "ro" }, { "russian federation", "ru" }, { "rwanda", "rw" },
    { "rwandese republic", "rw" }, { "réunion", "re" }, { "saint barthelemy", "bl" }, { "saint barthélemy", "bl" }, { "saint kitts and nevis", "kn" }, { "saint lucia", "lc" },
    { "saint martin (french part)", "mf" }, { "saint pierre and miquelon", "pm" }, { "saint vincent and the grenadines", "vc" }, { "samoa", "ws" }, { "san marino", "sm" },
    { "sao tome and principe", "st" }, { "saudi arabia", "sa" }, { "senegal", "sn" }, { "serbia", "rs" }, { "seychelles", "sc" }, { "sierra leone", "sl" }, { "singapore", "sg" },
    { "sint maarten (dutch part)", "sx" }, { "slovak republic", "sk" }, { "slovakia", "sk" }, { "slovenia", "si" }, { "socialist republic of viet nam", "vn" },
    { "solomon islands", "sb" }, { "somalia", "so" }, { "south africa", "za" }, { "south georgia and the south sandwich islands", "gs" }, { "south korea", "kr" },
    { "south sudan", "ss" }, { "spain", "es" }, { "sri lanka", "lk" }, { "state of israel", "il" }, { "state of kuwait", "kw" }, { "state of qatar", "qa" }, { "sudan", "sd" },
    { "sultanate of oman", "om" }, { "suriname", "sr" }, { "svalbard and jan mayen", "sj" }, { "sweden", "se" }, { "swiss confederation", "ch" }, { "switzerland", "ch" },
    { "syria", "sy" }, { "syrian arab republic", "sy" }, { "taiwan", "tw" }, { "tajikistan", "tj" }, { "tanzania", "tz" }, { "thailand", "th" }, { "the state of eritrea", "er" },
    { "the state of palestine", "ps" }, { "timor-leste", "tl" }, { "togo", "tg" }, { "togolese republic", "tg" }, { "tokelau", "tk" }, { "tonga", "to" },
    { "trinidad and tobago", "tt" }, { "tunisia", "tn" }, { "turkiye", "tr" }, { "turkmenistan", "tm" }, { "turks and caicos islands", "tc" }, { "tuvalu", "tv" },
    { "türkiye", "tr" }, { "uganda", "ug" }, { "ukraine", "ua" }, { "union of the comoros", "km" }, { "united arab emirates", "ae" }, { "united kingdom", "gb" },
    { "united kingdom of great britain and northern ireland", "gb" }, { "united mexican states", "mx" }, { "united republic of tanzania", "tz" }, { "united states", "us" },
    { "united states minor outlying islands", "um" }, { "united states of america", "us" }, { "uruguay", "uy" }, { "uzbekistan", "uz" }, { "vanuatu", "vu" }, { "venezuela", "ve" },
    { "viet nam", "vn" }, { "vietnam", "vn" }, { "virgin islands of the united states", "vi" }, { "wallis and futuna", "wf" }, { "western sahara", "eh" }, { "yemen", "ye" },
    { "zambia", "zm" }, { "zimbabwe", "zw" }, { "åland islands", "ax" },
};

std::vector<std::string> split_query(const std::string& query) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : query) {
        if (c == ',') {
            parts.push_back(trim(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    parts.push_back(trim(cur));
    std::vector<std::string> out;
    for (const std::string& p : parts) {
        if (!p.empty()) out.push_back(p);
    }
    return out;
}

std::string alias_code(const std::string& qualifier) {
    const std::string q = lower(qualifier);
    for (const CountryAlias& a : country_aliases) {
        if (q == a.alias) return a.code;
    }
    for (const CountryAlias& a : country_names) {
        if (q == a.alias) return a.code;
    }
    return std::string();
}

bool qualifier_matches(const std::string& qualifier, const fizmo::json::Value& r) {
    const std::string q = lower(qualifier);
    const std::string code = lower(r["country_code"].as_string());
    if (q == lower(r["country"].as_string()) || q == code) return true;
    if (!alias_code(q).empty() && alias_code(q) == code) return true;
    for (const char* key : { "admin1", "admin2", "admin3", "admin4" }) {
        const std::string admin = lower(r[key].as_string());
        if (admin.empty()) continue;
        if (admin == q) return true;
        if (q.size() >= 4 && (admin.find(q) != std::string::npos || q.find(admin) != std::string::npos)) return true;
        for (const Region& g : regions) {
            if (q == g.abbreviation && code == g.country && admin == g.name) return true;
        }
    }
    if (q.size() >= 4) {
        const std::string country = lower(r["country"].as_string());
        if (!country.empty() && (country.find(q) != std::string::npos || q.find(country) != std::string::npos)) return true;
    }
    return false;
}

void ensure_loop() {
    static std::once_flag once;
    std::call_once(once, [] {
        if (!fizmo::networking::background_running()) fizmo::networking::start_background(1);
    });
}

} // namespace

std::string weather_condition(int code) {
    switch (code) {
    case 0: return "Clear";
    case 1: return "Mostly clear";
    case 2: return "Partly cloudy";
    case 3: return "Overcast";
    case 45: return "Fog";
    case 48: return "Freezing fog";
    case 51: return "Light drizzle";
    case 53: return "Drizzle";
    case 55: return "Heavy drizzle";
    case 56: return "Light freezing drizzle";
    case 57: return "Freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: return "Light freezing rain";
    case 67: return "Freezing rain";
    case 71: return "Light snow";
    case 73: return "Snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80: return "Light showers";
    case 81: return "Showers";
    case 82: return "Heavy showers";
    case 85: return "Light snow showers";
    case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96: return "Thunderstorm with hail";
    case 99: return "Thunderstorm with heavy hail";
    default: return code < 0 ? std::string() : std::string("Unknown");
    }
}

std::string weather_icon(int code, bool is_day) {
    if (code < 0) return std::string();
    if (code == 0) return is_day ? "\xe2\x98\x80\xef\xb8\x8f" : "\xf0\x9f\x8c\x99";
    if (code <= 2) return is_day ? "\xe2\x9b\x85" : "\xe2\x98\x81\xef\xb8\x8f";
    if (code == 3) return "\xe2\x98\x81\xef\xb8\x8f";
    if (code == 45 || code == 48) return "\xf0\x9f\x8c\xab\xef\xb8\x8f";
    if (code >= 71 && code <= 77) return "\xf0\x9f\x8c\xa8\xef\xb8\x8f";
    if (code == 85 || code == 86) return "\xf0\x9f\x8c\xa8\xef\xb8\x8f";
    if (code >= 95) return "\xe2\x9b\x88\xef\xb8\x8f";
    return "\xf0\x9f\x8c\xa7\xef\xb8\x8f";
}

std::string url_encode(const std::string& text) {
    std::string out;
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

std::optional<Place> parse_geocode(const std::string& json, const std::string& query, std::string* error) {
    fizmo::json::Value root;
    std::string why;
    if (!fizmo::json::parse(json, root, &why) || !root.is_object()) {
        if (error) *error = "the place search didn't answer with JSON";
        return std::nullopt;
    }
    const auto& results = root["results"].items();
    if (results.empty()) {
        if (error) *error = "couldn't find \"" + query + "\"";
        return std::nullopt;
    }
    const std::vector<std::string> parts = split_query(query);
    const fizmo::json::Value* pick = &results[0];
    if (parts.size() > 1) {
        pick = nullptr;
        for (const auto& r : results) {
            bool all = true;
            for (std::size_t i = 1; i < parts.size() && all; ++i) all = qualifier_matches(parts[i], r);
            if (all) {
                pick = &r;
                break;
            }
        }
        if (!pick) {
            if (error) {
                std::string where;
                for (std::size_t i = 1; i < parts.size(); ++i) where += (i > 1 ? ", " : "") + parts[i];
                std::string seen;
                std::size_t n = 0;
                for (const auto& r : results) {
                    if (n == 3) break;
                    std::string label = r["name"].as_string();
                    const std::string admin = r["admin1"].as_string();
                    if (!admin.empty()) label += ", " + admin;
                    label += ", " + r["country"].as_string(r["country_code"].as_string());
                    seen += (n ? "; " : "") + label;
                    ++n;
                }
                *error = "couldn't find \"" + parts[0] + "\" in " + where + (seen.empty() ? std::string() : " (found " + seen + ")");
            }
            return std::nullopt;
        }
    }
    Place p;
    p.latitude = (*pick)["latitude"].as_number();
    p.longitude = (*pick)["longitude"].as_number();
    p.name = (*pick)["name"].as_string();
    const std::string admin = (*pick)["admin1"].as_string();
    const std::string country = (*pick)["country_code"].as_string();
    if (!admin.empty()) p.name += ", " + admin;
    else if (!country.empty()) p.name += ", " + country;
    return p;
}

std::string coordinate_text(double v) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.8f", v);
    std::string out = buf;
    while (out.find('.') != std::string::npos && (out.back() == '0' || out.back() == '.')) {
        const bool dot = out.back() == '.';
        out.pop_back();
        if (dot) break;
    }
    if (out == "-0") out = "0";
    return out;
}

std::string geocode_query_name(const std::string& query) {
    const std::vector<std::string> parts = split_query(query);
    return parts.empty() ? std::string() : parts[0];
}

std::string geocode_country(const std::string& query) {
    const std::vector<std::string> parts = split_query(query);
    std::string found;
    for (std::size_t i = 1; i < parts.size(); ++i) {
        const std::string q = lower(parts[i]);
        std::string code = alias_code(q);
        if (code.empty()) {
            for (const Region& g : regions) {
                if (q == g.name) code = g.country;
            }
        }
        if (code.empty() && q.size() == 2) {
            bool is_region = false;
            for (const Region& g : regions) is_region = is_region || q == g.abbreviation;
            if (!is_region) code = q;
        }
        if (!code.empty()) found = code;
    }
    for (char& c : found) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return found;
}

std::optional<WeatherSnapshot> parse_forecast(const std::string& json, std::string* error) {
    fizmo::json::Value root;
    std::string why;
    if (!fizmo::json::parse(json, root, &why) || !root.is_object()) {
        if (error) *error = "the weather service didn't answer with JSON";
        return std::nullopt;
    }
    if (root["error"].as_bool(false)) {
        if (error) *error = root["reason"].as_string("the weather service refused the request");
        return std::nullopt;
    }
    const fizmo::json::Value& c = root["current"];
    if (!c.is_object() || !c["temperature_2m"].is_number()) {
        if (error) *error = "the weather answer has no current conditions";
        return std::nullopt;
    }
    WeatherSnapshot w;
    w.ok = true;
    w.temp_c = c["temperature_2m"].as_number();
    w.feels_c = c["apparent_temperature"].as_number(w.temp_c);
    w.humidity = c["relative_humidity_2m"].as_number();
    w.wind_kmh = c["wind_speed_10m"].as_number();
    w.code = static_cast<int>(c["weather_code"].as_number(-1));
    w.is_day = c["is_day"].as_number(1) != 0.0;
    w.updated = std::chrono::system_clock::now();
    return w;
}

Weather::Weather() = default;

Weather::~Weather() {
    stop();
}

void Weather::configure(const WeatherConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (config == m_config && m_running.load()) return;
    m_config = config;
    m_dirty = true;
    if (!config.usable()) {
        m_snapshot = WeatherSnapshot();
        m_snapshot.error = config.coordinates ? "set a latitude and longitude" : "set a city";
    } else {
        m_snapshot.loading = true;
    }
    if (!m_running.exchange(true)) m_thread = std::thread([this] { run(); });
    m_wake.notify_all();
}

void Weather::refresh() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_dirty = true;
    m_wake.notify_all();
}

void Weather::stop() {
    if (!m_running.exchange(false)) return;
    m_wake.notify_all();
    if (m_thread.joinable()) m_thread.join();
}

WeatherSnapshot Weather::snapshot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_snapshot;
}

bool Weather::fetch(const std::string& url, std::string& body, std::string& error) {
    ensure_loop();
    for (int attempt = 0; attempt < 2; ++attempt) {
        std::string target = url;
        if (attempt == 1) {
            if (target.compare(0, 8, "https://") != 0) break;
            target = "http://" + target.substr(8);
        }
        auto promise = std::make_shared<std::promise<std::pair<std::string, int>>>();
        auto result = promise->get_future();
        http::FetchOptions options;
        options.timeout = std::chrono::milliseconds(12000);
        options.headers.set("User-Agent", "Pollen (VRChat OSC app)");
        http::fetch(target, options, [promise](const std::error_code& ec, http::FetchResponse r) {
            try {
                promise->set_value({ ec ? std::string("error: ") + ec.message() : r.body, ec ? -1 : r.status });
            } catch (...) {
            }
        });
        if (result.wait_for(std::chrono::seconds(15)) != std::future_status::ready) {
            error = "the weather service didn't answer";
            continue;
        }
        const auto got = result.get();
        if (got.second == 200) {
            body = got.first;
            return true;
        }
        error = got.second < 0 ? "couldn't reach the weather service (" + got.first.substr(7) + ")" : "the weather service answered " + std::to_string(got.second);
    }
    return false;
}

void Weather::run() {
    std::chrono::steady_clock::time_point next = std::chrono::steady_clock::now();
    while (m_running.load()) {
        WeatherConfig config;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait_until(lock, next, [this] { return !m_running.load() || m_dirty; });
            if (!m_running.load()) break;
            m_dirty = false;
            config = m_config;
        }
        const double minutes = config.refresh_minutes < 5.0 ? 5.0 : config.refresh_minutes;
        next = std::chrono::steady_clock::now() + std::chrono::seconds(static_cast<long long>(minutes * 60.0));
        if (!config.usable()) continue;
        std::string error, body;
        Place place;
        if (config.coordinates) {
            place.latitude = config.latitude;
            place.longitude = config.longitude;
            place.name = coordinate_text(config.latitude) + ", " + coordinate_text(config.longitude);
        } else if (m_geocoded_city == config.city) {
            place = m_place;
        } else {
            const std::string name = geocode_query_name(config.city);
            const std::string base = config.geocode_url + "?name=" + url_encode(name) + "&count=100&language=en&format=json";
            std::optional<Place> found;
            if (fetch(base, body, error)) found = parse_geocode(body, config.city, &error);
            const std::string country = geocode_country(config.city);
            if (!found && !country.empty()) {
                std::string second_error;
                if (fetch(base + "&countryCode=" + country, body, second_error)) found = parse_geocode(body, config.city, &second_error);
                if (!found && error.empty()) error = second_error;
            }
            if (found) {
                place = *found;
                m_place = place;
                m_geocoded_city = config.city;
            }
            if (m_geocoded_city != config.city) {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_snapshot.loading = false;
                m_snapshot.ok = false;
                m_snapshot.error = error;
                next = std::chrono::steady_clock::now() + std::chrono::minutes(2);
                continue;
            }
        }
        const std::string url = config.forecast_url + "?latitude=" + number(place.latitude) + "&longitude=" + number(place.longitude) +
            "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m,is_day&timezone=auto";
        std::optional<WeatherSnapshot> w;
        if (fetch(url, body, error)) w = parse_forecast(body, &error);
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_config != config) continue;
        if (w) {
            m_snapshot = *w;
            m_snapshot.place = place.name;
        } else {
            m_snapshot.loading = false;
            m_snapshot.error = error;
            next = std::chrono::steady_clock::now() + std::chrono::minutes(2);
        }
    }
}

} // namespace pollen
