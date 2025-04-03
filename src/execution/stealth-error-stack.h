#ifndef STEALTH_ERROR_STACK_H_
#define STEALTH_ERROR_STACK_H_

#include <string>

namespace v8 {
    namespace internal {

    namespace stealth_stack {

        std::string SetSourceURL(std::string script, bool wrapWithParenthesis);

        // Returns the unique CDP identifier for this program run.
        // The identifier is unique so that bot protection cannot 
        // influence the output by naming the Error or file names the same as our identifier.
        // Thread-safe, lazy-initialized on first call
        const std::string& GetStealthScriptName();

        // Checks if a stack trace or source URL contains the CDP identifier
        bool ContainsStealthScriptName(const std::string& text);

        std::string FormatStackTrace(const std::string& stackTrace);
        }

    }  // namespace internal
}  // namespace v8

#endif  // STEALTH_ERROR_STACK_H_