#include "RefactorTool.h"
#include <clang/Tooling/Tooling.h>
#include <gtest/gtest.h>
#include <memory>

class CodeRefactorActionTesting : public CodeRefactorAction {
public:
    CodeRefactorActionTesting() { RewriterBuffer.clear(); }

    virtual void EndSourceFileAction() override {
        // Сохраняем изменения в буфер.
        auto &EditBuffer =
            RewriterForCodeRefactor.getEditBuffer(RewriterForCodeRefactor.getSourceMgr().getMainFileID());
        llvm::raw_string_ostream OsStr(RewriterBuffer);
        EditBuffer.write(OsStr);
    };

    static const std::string &GetRewriterBuffer() { return RewriterBuffer; }

private:
    static std::string RewriterBuffer;
};

std::string CodeRefactorActionTesting::RewriterBuffer{};
const std::string FileName{"/tmp/test.cpp"};

TEST(ClassDeclTest, StandaloneCheck){
    std::string Code{R"(
        class Standalone {  // Нет наследников, не меняется
        public:
            ~Standalone() {}
        };
    )"};

    EXPECT_TRUE(clang::tooling::runToolOnCode(std::make_unique<CodeRefactorActionTesting>(), Code, FileName));
    EXPECT_EQ(CodeRefactorActionTesting::GetRewriterBuffer(), Code);
}

TEST(ClassDeclTest, NoChangeCheck){
    std::string Code{R"(
        class BaseVirtual {  // Уже виртуальный деструктор, не меняется
        public:
            virtual ~BaseVirtual() {}
        };
        class DerivedFromVirtual : public BaseVirtual {};
    )"};

    EXPECT_TRUE(clang::tooling::runToolOnCode(std::make_unique<CodeRefactorActionTesting>(), Code, FileName));
    EXPECT_EQ(CodeRefactorActionTesting::GetRewriterBuffer(), Code);
}

TEST(ClassDeclTest, BaseNoDtorCheck) {
    std::string Code{R"(
        class BaseNoDtor {
        public:
            int x;
            ~BaseNoDtor() = default;
        };
        class DerivedFromNoDtor : public BaseNoDtor {};
    )"};
    std::string Expected{R"(
        class BaseNoDtor {
        public:
            int x;
            virtual ~BaseNoDtor() = default;
        };
        class DerivedFromNoDtor : public BaseNoDtor {};
    )"};

    EXPECT_TRUE(clang::tooling::runToolOnCode(std::make_unique<CodeRefactorActionTesting>(), Code, FileName));
    EXPECT_EQ(CodeRefactorActionTesting::GetRewriterBuffer(), Expected);
}

TEST(ClassDeclTest, OnceVirtualCheck) {
    std::string Code{R"(
        class BaseNonVirtual {  // Невиртуальный деструктор
        public:
            ~BaseNonVirtual() {}  //Убедимся что один раз virtual
        };
        class DerivedFromNonVirtual : public BaseNonVirtual {};
        class DerivedFromNonVirtual1 : public DerivedFromNonVirtual {};
    )"};
    std::string Expected{R"(
        class BaseNonVirtual {  // Невиртуальный деструктор
        public:
            virtual ~BaseNonVirtual() {}  //Убедимся что один раз virtual
        };
        class DerivedFromNonVirtual : public BaseNonVirtual {};
        class DerivedFromNonVirtual1 : public DerivedFromNonVirtual {};
    )"};

    EXPECT_TRUE(clang::tooling::runToolOnCode(std::make_unique<CodeRefactorActionTesting>(), Code, FileName));
    EXPECT_EQ(CodeRefactorActionTesting::GetRewriterBuffer(), Expected);
}

TEST(MethodDeclTest, NoOverrideCheck){
    std::string Code{R"(
        class BaseWithOverride {
        public:
            virtual void func() {}
        };

        class DerivedWithOverride : public BaseWithOverride {
        public:
            void func() override {}  // Уже с override, не меняется
        };
    )"};

    EXPECT_TRUE(clang::tooling::runToolOnCode(std::make_unique<CodeRefactorActionTesting>(), Code, FileName));
    EXPECT_EQ(CodeRefactorActionTesting::GetRewriterBuffer(), Code);
}

TEST(MethodDeclTest, FuncOverrideCheck){
    std::string Code{R"(
        class Base {
        public:
            virtual void func() {}
            virtual void func(int a) = 0;
        };

        class Derived : public Base {
        public:
            void func() {}  // Переопределен без override
            void func(int a);  // Переопределен без override
        };
        void Derived::func(int a) {}
    )"};
    std::string Expected{R"(
        class Base {
        public:
            virtual void func() {}
            virtual void func(int a) = 0;
        };

        class Derived : public Base {
        public:
            void func()  override {}  // Переопределен без override
            void func(int a) override ;  // Переопределен без override
        };
        void Derived::func(int a) {}
    )"};

    EXPECT_TRUE(clang::tooling::runToolOnCode(std::make_unique<CodeRefactorActionTesting>(), Code, FileName));
    EXPECT_EQ(CodeRefactorActionTesting::GetRewriterBuffer(), Expected);
}

TEST(MethodDeclTest, NoDestructorOverrideCheck){
    std::string Code{R"(
        class Base {
        public:
            virtual ~Base() {}
        };
        class Derived : public Base {
        public:
            ~Derived() {}  // Деструктор без override
        };
    )"};

    EXPECT_TRUE(clang::tooling::runToolOnCode(std::make_unique<CodeRefactorActionTesting>(), Code, FileName));
    EXPECT_EQ(CodeRefactorActionTesting::GetRewriterBuffer(), Code);
}

TEST(VarDeclTest, NoChangeCheck){
    std::string Code{R"(
        #include <vector>
        #include <string>

        struct CustomType {
            int id;
            std::string name;
        };

        int main(){
            std::vector<CustomType> vec = {{1, "a"}, {2, "b"}};
            // Уже с &
            for (const auto& x : vec) {}
        }
    )"};

    EXPECT_TRUE(clang::tooling::runToolOnCode(std::make_unique<CodeRefactorActionTesting>(), Code, FileName));
    EXPECT_EQ(CodeRefactorActionTesting::GetRewriterBuffer(), Code);
}

TEST(VarDeclTest, BasicTypeNoChangeCheck){
    std::string Code{R"(
        #include <vector>
        int main(){
            // Фундаментальный тип, не меняется
            std::vector<int> ints = {1, 2};
            for (const int x : ints) {}
        }
    )"};

    EXPECT_TRUE(clang::tooling::runToolOnCode(std::make_unique<CodeRefactorActionTesting>(), Code, FileName));
    EXPECT_EQ(CodeRefactorActionTesting::GetRewriterBuffer(), Code);
}

TEST(VarDeclTest, NoConstNoChangeCheck) {
    std::string Code{R"(
        #include <vector>
        #include <string>

        struct CustomType {
            int id;
            std::string name;
        };

        int main(){
            std::vector<CustomType> vec = {{1, "a"}, {2, "b"}};

            for (auto x : vec) {}
            for (CustomType x : vec) {}
            for (decltype(vec)::value_type x : vec) {}
        }
    )"};

    EXPECT_TRUE(clang::tooling::runToolOnCode(std::make_unique<CodeRefactorActionTesting>(), Code, FileName));
    EXPECT_EQ(CodeRefactorActionTesting::GetRewriterBuffer(), Code);
}

TEST(VarDeclTest, ConstNoRefChangeCheck){
    std::string Code{R"(
        #include <vector>
        #include <string>

        struct CustomType {
            int id;
            std::string name;
        };

        int main(){
            std::vector<CustomType> vec = {{1, "a"}, {2, "b"}};

            // const auto без &
            for (const auto x : vec) {}

            // const явный тип без &
            for (const CustomType x : vec) {}

            // const decltype без &
            for (const decltype(vec)::value_type x : vec) {}
        }
    )"};
    std::string Expected{R"(
        #include <vector>
        #include <string>

        struct CustomType {
            int id;
            std::string name;
        };

        int main(){
            std::vector<CustomType> vec = {{1, "a"}, {2, "b"}};

            // const auto без &
            for (const auto &x : vec) {}

            // const явный тип без &
            for (const CustomType &x : vec) {}

            // const decltype без &
            for (const decltype(vec)::value_type &x : vec) {}
        }
    )"};

    EXPECT_TRUE(clang::tooling::runToolOnCode(std::make_unique<CodeRefactorActionTesting>(), Code, FileName));
    EXPECT_EQ(CodeRefactorActionTesting::GetRewriterBuffer(), Expected);
}