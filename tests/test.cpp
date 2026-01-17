#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

constexpr auto TOOL_PATH = "../build/refactor_tool";
constexpr auto TEST_FILENAME = "temp_test.cpp";

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