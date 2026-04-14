#include "rest_validation.h"

#include <cctype>
#include <cstdlib>
#include <vector>

namespace rbs::api {

static bool hasJsonKey(const std::string& body, const std::string& key) {
    return body.find('"' + key + '"') != std::string::npos;
}

static bool extractJsonInt64(const std::string& body, const std::string& key, long long& out) {
    auto it = body.find('"' + key + '"');
    if (it == std::string::npos) return false;
    it = body.find(':', it);
    if (it == std::string::npos) return false;
    ++it;
    while (it < body.size() && std::isspace(static_cast<unsigned char>(body[it]))) ++it;
    if (it >= body.size()) return false;
    char* end = nullptr;
    out = std::strtoll(body.c_str() + static_cast<long long>(it), &end, 10);
    return end != body.c_str() + static_cast<long long>(it);
}

ValidationResult validateInjectBody(const std::string& body) {
    if (!hasJsonKey(body, "procedure")) {
        return ValidationResult::fail("missing required field 'procedure'");
    }
    return ValidationResult::success();
}

ValidationResult validateHandoverBody(const std::string& body) {
    long long cellId = -1;
    long long rnti = -1;
    long long targetPci = -1;
    long long targetEarfcn = -1;

    if (!extractJsonInt64(body, "cellId", cellId) || cellId <= 0) {
        return ValidationResult::fail("invalid or missing 'cellId'");
    }
    if (!extractJsonInt64(body, "rnti", rnti) || rnti <= 0) {
        return ValidationResult::fail("invalid or missing 'rnti'");
    }
    if (!extractJsonInt64(body, "targetPci", targetPci) || targetPci < 0) {
        return ValidationResult::fail("invalid or missing 'targetPci'");
    }
    if (!extractJsonInt64(body, "targetEarfcn", targetEarfcn) || targetEarfcn < 0) {
        return ValidationResult::fail("invalid or missing 'targetEarfcn'");
    }

    return ValidationResult::success();
}

ValidationResult validateConfigPatchBody(const std::string& body) {
    const bool hasUpdates = hasJsonKey(body, "updates");
    const bool hasReload = hasJsonKey(body, "reloadFromDisk");
    const bool hasSection = hasJsonKey(body, "section");
    const bool hasKey = hasJsonKey(body, "key");
    const bool hasValue = hasJsonKey(body, "value");

    if (hasUpdates) {
        if (body.find('[') == std::string::npos || body.find(']') == std::string::npos) {
            return ValidationResult::fail("field 'updates' must be an array");
        }
        return ValidationResult::success();
    }

    if (hasReload) {
        return ValidationResult::success();
    }

    if ((hasSection || hasKey || hasValue) && !(hasSection && hasKey)) {
        return ValidationResult::fail("'section' and 'key' must be provided together");
    }

    if (hasSection && hasKey) {
        return ValidationResult::success();
    }

    return ValidationResult::fail("nothing to apply: provide updates[], reloadFromDisk or section/key");
}

}  // namespace rbs::api
