#pragma once

#include <string>

namespace rbs::api {

struct ValidationResult {
    bool ok = false;
    std::string error;

    static ValidationResult success() {
        return ValidationResult{true, ""};
    }

    static ValidationResult fail(std::string msg) {
        return ValidationResult{false, std::move(msg)};
    }
};

ValidationResult validateInjectBody(const std::string& body);
ValidationResult validateHandoverBody(const std::string& body);
ValidationResult validateConfigPatchBody(const std::string& body);

}  // namespace rbs::api
