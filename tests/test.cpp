#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

constexpr auto TOOL_PATH = "../build/refactor_tool";
constexpr auto TEST_FILENAME = "temp_test.cpp";
constexpr auto COMPILER_ARGS = "-std=c++17 -fsanitize=address,undefined -g";

void RunToolAndCompareContents(const std::string &initial_content, const std::string &expected_after_refactor_cont) {
    if (!std::filesystem::exists(TOOL_PATH)) {
        FAIL() << "Refactor tool not found at path: " << TOOL_PATH;
    }

    std::string tempFilePath = TEST_FILENAME;
    std::ofstream tempFile(tempFilePath);
    tempFile << initial_content;
    tempFile.close();

    std::string command = std::string{TOOL_PATH} + " " + tempFilePath;
    system(command.c_str());

    std::ifstream resultFile(tempFilePath);
    std::string resultCode((std::istreambuf_iterator<char>(resultFile)), std::istreambuf_iterator<char>());
    resultFile.close();

    EXPECT_EQ(resultCode, expected_after_refactor_cont);

    if (std::filesystem::exists(tempFilePath)) {
        std::filesystem::remove(tempFilePath);
    }
}

TEST(BasicCheck, CheckBaseClassDtorBecomesVirtual) {
    std::string intitial = R"cpp(
        class Base {
        public:
            ~Base() {}
        };

        class Derived : public Base {
        public:
            ~Derived() {}
        };
    )cpp";
    std::string expected = R"cpp(
        class Base {
        public:
            virtual ~Base() {}
        };

        class Derived : public Base {
        public:
            ~Derived() {}
        };
    )cpp";
    RunToolAndCompareContents(intitial, expected);
}

TEST(BasicCheck, CheckDtorIsAlreadyVirtual) {
    std::string intitial = R"cpp(
        class Base {
        public:
            virtual ~Base() {}
        };

        class Derived : public Base {
        public:
            ~Derived() {}
        };
    )cpp";
    std::string expected = intitial;
    RunToolAndCompareContents(intitial, expected);
}

TEST(BasicCheck, NoDerivedClassesDtorNotChanged) {
    std::string intitial = R"cpp(
        class Standalone {
        public:
            ~Standalone() {}
        };
    )cpp";
    std::string expected = intitial;
    RunToolAndCompareContents(intitial, expected);
}

TEST(BasicCheck, CheckOverrideAdded) {
    std::string intitial = R"cpp(
        class Base {
        public:
            virtual void foo() {}
        };

        class Derived : public Base {
        public:
            void foo() {}
        };
    )cpp";
    std::string expected = R"cpp(
        class Base {
        public:
            virtual void foo() {}
        };

        class Derived : public Base {
        public:
            void foo() override {}
        };
    )cpp";
    RunToolAndCompareContents(intitial, expected);
}

TEST(BasicCheck, CheckOverrideNotAddedIfAlreadyPresent) {
    std::string intitial = R"cpp(
        class Base {
        public:
            virtual void foo() {}
        };

        class Derived : public Base {
        public:
            void foo() override {}
        };
    )cpp";
    std::string expected = intitial;
    RunToolAndCompareContents(intitial, expected);
}

TEST(BasicCheck, ConstNoexceptPreservedWhenAddingOverride) {
    std::string intitial = R"cpp(
        class Base {
        public:
            virtual void foo() const noexcept {}
        };

        class Derived : public Base {
        public:
            void foo() const noexcept {}
        };
    )cpp";
    std::string expected = R"cpp(
        class Base {
        public:
            virtual void foo() const noexcept {}
        };

        class Derived : public Base {
        public:
            void foo() const noexcept override {}
        };
    )cpp";
    RunToolAndCompareContents(intitial, expected);
}

TEST(BasicCheck, LvalueRefNoexceptPreservedWhenAddingOverride) {
    std::string intitial = R"cpp(
        class Base {
        public:
            virtual void foo() & noexcept {}
        };

        class Derived : public Base {
        public:
            void foo() & noexcept {}
        };
    )cpp";
    std::string expected = R"cpp(
        class Base {
        public:
            virtual void foo() & noexcept {}
        };

        class Derived : public Base {
        public:
            void foo() & noexcept override {}
        };
    )cpp";
    RunToolAndCompareContents(intitial, expected);
}

TEST(BasicCheck, ConstIterDoesNotBecomeRefForFundamentalType) {
    std::string intitial = R"cpp(
        #include <vector>

        void func(const std::vector<int>& vec) {
            for (const int item : vec) {
                // Do something with item
            }
        }
    )cpp";

    RunToolAndCompareContents(intitial, intitial);
}

TEST(BasicCheck, ConstIterBecomesRefForNonFundamentalType) {
    std::string intitial = R"cpp(
        #include <vector>

        class Hello {};

        void func(const std::vector<Hello>& vec) {
            for (const Hello item : vec) {
                // Do something with item
            }
        }
    )cpp";
    std::string expected = R"cpp(
        #include <vector>

        class Hello {};

        void func(const std::vector<Hello>& vec) {
            for (const Hello &item : vec) {
                // Do something with item
            }
        }
    )cpp";
    RunToolAndCompareContents(intitial, expected);
}

void CopyFileToTemp(const std::string &sourcePath, const std::string &tempPath) {
    std::ifstream src(sourcePath, std::ios::binary);
    std::ofstream dst(tempPath, std::ios::binary);
    dst << src.rdbuf();
}

TEST(BasicCheck, LeakExampleNoLeaksAfterRefactor) {
    std::string pathToLeakExample = "./tests_data/leak_example.cpp";
    if (!std::filesystem::exists("./temp")) {
        std::filesystem::create_directory("./temp");
    }
    CopyFileToTemp(pathToLeakExample, "./temp/leak_example.cpp");
    pathToLeakExample = "./temp/leak_example.cpp";

    // run tool to refactor the code
    std::string tool_cmd = std::string{TOOL_PATH} + " " + pathToLeakExample;
    system(tool_cmd.c_str());

    // compile the refactored code with ASAN
    std::string command = "g++ " + std::string{COMPILER_ARGS} + " " + pathToLeakExample + " -o leak_example_test";

    int compileResult = system(command.c_str());
    ASSERT_EQ(compileResult, 0) << "Compilation failed for leak_example.cpp";

    std::string runCommand = "./leak_example_test";
    int runResult = system(runCommand.c_str());

    ASSERT_EQ(runResult, 0) << "Execution failed for leak_example_test";
    // Clean up the compiled binary
    if (std::filesystem::exists("leak_example_test")) {
        std::filesystem::remove("leak_example_test");
    }
    if (std::filesystem::exists("./temp/leak_example.cpp")) {
        std::filesystem::remove("./temp/leak_example.cpp");
    }
}

auto CompileAndMeasureRuntime(const std::string &sourceFilePath, const std::string &binaryName) {
    std::string command = "g++ " + sourceFilePath + " -o " + binaryName;

    int compileResult = system(command.c_str());
    if (compileResult != 0) {
        throw std::runtime_error("Compilation failed for " + sourceFilePath);
    }

    auto start = std::chrono::high_resolution_clock::now();
    std::string runCommand = "./" + binaryName;
    int runResult = system(runCommand.c_str());
    auto end = std::chrono::high_resolution_clock::now();
    if (runResult != 0) {
        throw std::runtime_error("Execution failed for " + binaryName);
    }

    std::chrono::duration<double> duration = end - start;
    return duration.count();
}

TEST(BasicCheck, PerfExampleNoCopiesAfterRefactor) {
    std::string pathToPerfExample = "./tests_data/perf_example.cpp";
    if (!std::filesystem::exists("./temp")) {
        std::filesystem::create_directory("./temp");
    }
    CopyFileToTemp(pathToPerfExample, "./temp/perf_example.cpp");
    pathToPerfExample = "./temp/perf_example.cpp";

    auto before_refactor_dt = CompileAndMeasureRuntime(pathToPerfExample, "perf_example_before");

    // run tool to refactor the code
    std::string tool_cmd = std::string{TOOL_PATH} + " " + pathToPerfExample;
    system(tool_cmd.c_str());

    auto after_refactor_dt = CompileAndMeasureRuntime(pathToPerfExample, "perf_example_after");
    ASSERT_LT(after_refactor_dt, before_refactor_dt * 0.9)
        << "Refactored code is not significantly faster than the original";

    if (std::filesystem::exists("perf_example_before")) {
        std::filesystem::remove("perf_example_before");
    }
    if (std::filesystem::exists("perf_example_after")) {
        std::filesystem::remove("perf_example_after");
    }
}