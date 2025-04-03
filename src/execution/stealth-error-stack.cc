#include "src/execution/stealth-error-stack.h"
#include "base/no_destructor.h"
#include <random>
#include <mutex>
#include <sstream>
#include <ctime>
#include <vector>


namespace v8 {
  namespace internal {

    namespace stealth_stack {

      namespace {
  
        /**
         * Generates a random integer within an inclusive range [min, max].
         * 
         * @param min The minimum value (inclusive)
         * @param max The maximum value (inclusive)
         * @return A random integer between min and max (inclusive)
         */
        int generateRandomIntInclusive(int min, int max) {
            // Seed the random number generator.
            // Do not use std::random_device. It doesn't always work correctly with v8 isolates.
            static std::mt19937 rng(static_cast<unsigned int>(std::time(nullptr)));
            
            // Initialize a Mersenne Twister engine with the random seed
            std::mt19937 generator(rng());
            
            // Create a uniform distribution for integers in the range [min, max]
            std::uniform_int_distribution<int> distribution(min, max);
            
            // Generate and return a random number
            return distribution(generator);
        }
  
        // generates random line numbers like "343:5"
        std::string generateLineNumbers() {
          // Seed the random number generator.
          // Do not use std::random_device. It doesn't always work correctly with v8 isolates.
          static std::mt19937 rng(static_cast<unsigned int>(std::time(nullptr)));
          std::mt19937 gen(rng());
          
          // Define the distributions for the two numbers
          std::uniform_int_distribution<int> dist1(5, 2000);  // First number range: 5-2000
          std::uniform_int_distribution<int> dist2(1, 60);    // Second number range: 1-60
          
          // Generate the random numbers
          int num1 = dist1(gen);
          int num2 = dist2(gen);
          
          // Format the result as a string
          std::stringstream result;
          result << num1 << ":" << num2;
          
          return result.str();
        }
  
        std::string generateRandomFunctionName() {
          // Seed the random number generator
          static std::mt19937 rng(static_cast<unsigned int>(std::time(nullptr)));
          
          // Define the character set to use
          const std::string charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
          
          // Generate a random length between 3 and 10 (inclusive)
          std::uniform_int_distribution<int> lengthDist(3, 10);
          int length = lengthDist(rng);
          
          // Generate a random string of the chosen length
          std::uniform_int_distribution<size_t> charDist(0, charset.size() - 1);
          std::string randomString;
          randomString.reserve(length);
          
          for (int i = 0; i < length; ++i) {
              randomString += charset[charDist(rng)];
          }
          
          return randomString;
        }
  
        // Example: at plop1 (app.js:3:24)
        std::string generateStackLine(const std::string& scriptName, bool isLastLine) { 
          std::string result = "    at ";

          if (!isLastLine) {
            // In v8 js stack traces, if the line of the stack trace 
            // contains a function name, then parenthesis are placed
            // around the script and line numbers.
            // We are not using parenthesis on the last line, so 
            // we will not include a function name on the last line.
            result += generateRandomFunctionName() + " ";
            result += "(";
          }

          result += scriptName + ":" + generateLineNumbers();

          if (!isLastLine) {
            result += ")\n";
          }

          return result;
        }

        std::vector<std::string> split(const std::string& s, char delimiter) {
          std::vector<std::string> tokens;
          std::string token;
          std::istringstream tokenStream(s);
          while (std::getline(tokenStream, token, delimiter)) {
              tokens.push_back(token);
          }
          return tokens;
        }

        std::string join(const std::vector<std::string>& vec, const std::string& delimiter) {
          if (vec.empty()) {
              return "";
          }
          
          std::string result = vec[0];
          for (size_t i = 1; i < vec.size(); ++i) {
              result += delimiter + vec[i];
          }
          
          return result;
        }
  
        // remove all lines that contain the stealth script name
        std::string removeSubstringLines(const std::string& input, const std::string& substring) {
          bool previousLineWasUtilityScript = false;

          std::vector<std::string> lines = split(input, '\n');

          for (size_t i = lines.size(); i-- > 0;) {
              std::string line = lines[i];
              if (line.rfind(substring) != std::string::npos) {
                  // found. remove.

                  if (line.rfind("UtilityScript") != std::string::npos) {
                      previousLineWasUtilityScript = true;
                      // There can be multiple lines that contain UtilityScript
                      // and they may have "<anonymous>" as the script name.
                      // There will always be a single "at eval (eval at evaluate" line 
                      // after a UtilityScript line is found.
                  }
                  
                  lines.erase(lines.begin() + i);
                  continue;
              }

              // If the last line contains the substring "at eval (eval at evaluate" then we want to remove that line as well.
              // The reason we only remove the last line that says that, is that bot protection could use eval to try 
              // to see if we modify the stack. If we only remove the last one, we can be pretty sure we're not falling into a trap.
              if (previousLineWasUtilityScript && line.find("at eval (eval at evaluate") != std::string::npos) {
                  // found. remove.
                  lines.erase(lines.begin() + i);
              }

              // If the line contained neither our custom script name or "eval"
              // then we will end up here. We are done removing lines.
              break;
          }

          return join(lines, "\n");
        }

        void removeSourcURL(std::string& script) {
          const std::string kPrefix = "//# sourceURL=";
        
          size_t pos = script.rfind(kPrefix);
          if (pos == std::string::npos) return;
        
          // Backtrack to the beginning of the line
          size_t sourceURLStart = script.rfind('\n', pos);
          if (sourceURLStart == std::string::npos)
            sourceURLStart = pos; // Script is a single line. We will remove starting from "//# sourceURL=" instead of removing the line
        
          // Remove the line, or the rest of the line
          script.erase(sourceURLStart);
        }
      }  // namespace

      std::string SetSourceURL(std::string script, bool wrapWithParenthesis) {
        removeSourcURL(script);
        // Runtime.callFunctionOn requires wrapping with parenthesis.
        // Runtime.evaluate requires that we do not wrap with parenthesis.
        if (wrapWithParenthesis) {
          script = "(" + script + ")";
        }
        script += "\n//# sourceURL=" + GetStealthScriptName() + "\n";
        return script;
      }

      const std::string& GetStealthScriptName() {
        static const base::NoDestructor<std::string> g_script_name([] {
          unsigned seed = static_cast<unsigned>(std::time(nullptr));
          std::mt19937 gen(seed);
          std::uniform_int_distribution<uint32_t> dis;
      
          std::stringstream ss;
          ss << std::hex << dis(gen) << seed << dis(gen) << ".js";
          return ss.str();
        }());
      
        return *g_script_name;
      }
  
      bool ContainsStealthScriptName(const std::string& text) {
        return text.find(GetStealthScriptName()) != std::string::npos;
      }
  
      // Apply stealth evasion to stack trace
      std::string FormatStackTrace(const std::string& stackTrace) {
        std::string result = removeSubstringLines(stackTrace, GetStealthScriptName());
        result += "\n";
        int numLines = generateRandomIntInclusive(2,6);
  
        std::string scriptName = "app.js";
        bool isLastLine = false;
        for (int i = 0; i < numLines; i++) {
          if (i == numLines -1) {
            isLastLine = true;
          }
          result += generateStackLine(scriptName, isLastLine);
        }
  
        return result;
      }
      
    }

  }  // namespace internal
}  // namespace v8