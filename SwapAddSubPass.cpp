#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include <string>
#include <vector>
#include <queue>

using namespace llvm;

namespace {

    bool swapBinaryAddSubInFunction(Function &F) {

        bool FunctionChanged = false;                   // Флаг для отслеживания изменений внутри этой конкретной функции
        SmallVector<BinaryOperator *, 16> OpsToReplace; // Вектор указателей на инструкции, которые нужно заменить

        llvm::outs() << "  Scanning function: " << F.getName() << " for swappable Add/Sub...\n";

        for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) { // Итерация по всем инструкциям функции
            if (auto *BinOp = dyn_cast<BinaryOperator>(&*I)){ 
                Instruction::BinaryOps Opcode = BinOp->getOpcode();
                if (Opcode == Instruction::Add || Opcode == Instruction::Sub) {
                    llvm::outs() << "    Marking for swap in " << F.getName() << ": " << *BinOp << "\n";
                    OpsToReplace.push_back(BinOp); // Добавляем инструкцию в список для замены
                }
            }
        }

        // Если нет инструкций для замены, то ничего не меняем
        if (OpsToReplace.empty()) {
            llvm::outs() << "  No swappable Add/Sub found in " << F.getName() << ".\n";
            return false;
        }

        SmallVector<Instruction *, 16> instructionsToDelete; // Вектор для хранения старых инструкций, которые нужно удалить после замен

        // Меняем инструкции
        for (BinaryOperator *BinOp : OpsToReplace) {

            IRBuilder<> Builder(BinOp);
            // Получаем операнды старой инструкции
            Value *LHS = BinOp->getOperand(0);
            Value *RHS = BinOp->getOperand(1);
            const Twine &Name = BinOp->getName();

            // Сохраняем флаги nsw/nuw из старой инструкции
            bool HasNSW = false;
            bool HasNUW = false;
            if (auto *OverflowOp = dyn_cast<OverflowingBinaryOperator>(BinOp)) {
                HasNSW = OverflowOp->hasNoSignedWrap();
                HasNUW = OverflowOp->hasNoUnsignedWrap();
            }

            Instruction *NewInst = nullptr;
            // Создаем новую инструкцию с инвертированной операцией
            if (BinOp->getOpcode() == Instruction::Add) {
                llvm::outs() << "    Replacing BINARY ADD with SUB in " << F.getName() << "\n";
                NewInst = cast<Instruction>(Builder.CreateSub(LHS, RHS, Name, HasNUW, HasNSW));
            }
            else {
                llvm::outs() << "    Replacing BINARY SUB with ADD in " << F.getName() << "\n";
                NewInst = cast<Instruction>(Builder.CreateAdd(LHS, RHS, Name, HasNUW, HasNSW));
            }

            if (BinOp->hasMetadata()) {
                NewInst->copyMetadata(*BinOp);
            }

            BinOp->replaceAllUsesWith(NewInst);    // Заменяем все использования результата старой инструкции на результат новой
            instructionsToDelete.push_back(BinOp); // Добавляем старую инструкцию в список на удаление
            FunctionChanged = true;
        }

        // Удаляем старые инструкции
        for (Instruction *Inst : instructionsToDelete) {
            Inst->eraseFromParent();
        }

        llvm::outs() << "  Finished swapping in " << F.getName() << ".\n";
        return FunctionChanged;
    }

    struct SwapAddSubPass : public PassInfoMixin<SwapAddSubPass> {

        PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
            bool Changed = false;
            SmallVector<Function *, 8> rootFunctions;  // Хранит указатели на функции, содержащие "replace" в имени (корневые)
            DenseMap<Function *, Function *> cloneMap; // Мапа для отслеживания клонированных функций: ключ - оригинальная функция, значение - ее клон
            std::queue<Function *> cloneWorklist;      // Очередь для BFS: хранит оригинальные функции, которые нужно проверить на необходимость клонирования
            DenseSet<Function *> visitedForCloning;    // Множество для отслеживания оригинальные функций, которые уже были добавлены в cloneWorklist

            // Ищем replace и добавляем в rootFunction
            llvm::outs() << "SwapAddSubPass: Finding root functions...\n";
            for (Function &F : M) {
                if (F.isDeclaration()) {
                    continue;
                }
                if (F.getName().contains("replace")) {
                    rootFunctions.push_back(&F);
                    llvm::outs() << "  Found root function: " << F.getName() << "\n";
                }
            }

            if (rootFunctions.empty()) {
                return PreservedAnalyses::all();
            }

            // Ищем функции, которые непосредственно вызываются из replace
            llvm::outs() << "SwapAddSubPass: Collecting initial functions to clone...\n";
            for (Function *RootF : rootFunctions) {
                for (inst_iterator I = inst_begin(RootF), E = inst_end(RootF); I != E; ++I) { // Итерируемся по инструкциям функции
                    if (auto *Call = dyn_cast<CallInst>(&*I)) {

                        Function *CalledF = Call->getCalledFunction(); // Взываемая функция

                        if (CalledF && !CalledF->isDeclaration() && visitedForCloning.insert(CalledF).second) { // insert возвращает true, если эта функция еще не добавлялась
                            llvm::outs() << "  Adding " << CalledF->getName() << " (called by " << RootF->getName() << ") to clone worklist.\n";
                            cloneWorklist.push(CalledF); // Добавляем оригинальную вызываемую функцию в очередь на обработку
                        }
                    }
                }
            }

            // Итеративное клонирование и сбор вызываемых функций (BFS)
            llvm::outs() << "SwapAddSubPass: Iteratively cloning and collecting callees...\n";

            while (!cloneWorklist.empty()) { // Пока есть функции в очереди на обработку

                Function *OrigF = cloneWorklist.front(); // Берем следующую оригинальную функцию из очереди
                cloneWorklist.pop();

                if (cloneMap.count(OrigF)) { // Проверяем, не создали ли мы уже клон для этой функции ранее
                    llvm::outs() << "  Skipping " << OrigF->getName() << ", already processed.\n";
                    continue;
                }

                llvm::outs() << "  Cloning " << OrigF->getName() << "...\n";
                ValueToValueMapTy VMap;                         // Мапа для отслеживания соответствий в процессе клонирования
                Function *ClonedF = CloneFunction(OrigF, VMap); // Клонируем оригинальную функцию

                std::string ClonedName = (OrigF->getName() + "_swapped").str(); // Формируем имя для клона
                ClonedF->setName(ClonedName);                                   // Устанавливаем имя
                cloneMap[OrigF] = ClonedF;                                      // Заносим в мапу пару [Оригинал,Клон]
                llvm::outs() << "    -> Cloned to " << ClonedF->getName() << "\n";
                Changed = true;

                // Ищем вызовы функций внутри клона
                for (inst_iterator I = inst_begin(ClonedF), E = inst_end(ClonedF); I != E; ++I) {
                    if (auto *Call = dyn_cast<CallInst>(&*I)) {
                        Function *CalledOrigF = Call->getCalledFunction(); // Оригинальная функцию, на которую указывает вызов в клоне

                        if (CalledOrigF && !CalledOrigF->isDeclaration() && visitedForCloning.insert(CalledOrigF).second) {
                            llvm::outs() << "    Adding " << CalledOrigF->getName() << " (called by clone " << ClonedF->getName() << ") to clone worklist.\n";
                            cloneWorklist.push(CalledOrigF); // Добавляем оригинальную вызываемую функцию в очередь
                        }
                    }
                }
            }

            // Перенаправляем вызовы внутри клонов с оригиналов на другие клоны
            llvm::outs() << "SwapAddSubPass: Redirecting calls within clones...\n";

            for (auto const &[OrigF, ClonedF] : cloneMap) {
                SmallVector<CallInst *, 8> CallsToUpdateInClone; // Инструкции вызова внутри текущего клона

                for (inst_iterator I = inst_begin(ClonedF), E = inst_end(ClonedF); I != E; ++I) {
                    if (auto *Call = dyn_cast<CallInst>(&*I)) {
                        CallsToUpdateInClone.push_back(Call);
                    }
                }

                for (CallInst *Call : CallsToUpdateInClone) {

                    Function *CalledOrigF = Call->getCalledFunction(); // Оригинальная функция, на которую указывает вызов

                    if (CalledOrigF && cloneMap.count(CalledOrigF)) { // Проверяем, есть ли для этой вызываемой функции запись в мапе клонов

                        Function *TargetClone = cloneMap[CalledOrigF]; // Указатель на клон
                        llvm::outs() << "  In clone " << ClonedF->getName() << ": redirecting call to " << CalledOrigF->getName() << " -> " << TargetClone->getName() << "\n";
                        Call->setCalledFunction(TargetClone); // Теперь инструкция вызывает клон
                        Changed = true;
                    }
                }
            }

            // Модифицируем Add/Sub внутри клонов
            llvm::outs() << "SwapAddSubPass: Swapping Add/Sub within clones...\n";
            for (auto const &[OrigF, ClonedF] : cloneMap) {
                llvm::outs() << "  Processing clone: " << ClonedF->getName() << "\n";

                if (swapBinaryAddSubInFunction(*ClonedF)) { // Заменяем операции в клоне
                    Changed = true; // Обновляем флаг, если были изменения
                }
            }

            // Перенаправляем вызовы из оригинальных корневых функций на клоны
            llvm::outs() << "SwapAddSubPass: Redirecting calls from root functions...\n";

            for (Function *RootF : rootFunctions) {

                SmallVector<CallInst *, 8> CallsToUpdateInRoot; // Инструкции вызова внутри корневой функции
                for (inst_iterator I = inst_begin(RootF), E = inst_end(RootF); I != E; ++I) {
                    if (auto *Call = dyn_cast<CallInst>(&*I)) {
                        CallsToUpdateInRoot.push_back(Call);
                    }
                }

                for (CallInst *Call : CallsToUpdateInRoot) {

                    Function *CalledOrigF = Call->getCalledFunction(); // Оригинальная функция, вызываемая инструкцией Call
                    // Если для этой функции существует клон...
                    if (CalledOrigF && cloneMap.count(CalledOrigF)) {
                        Function *TargetClone = cloneMap[CalledOrigF]; // Указатель на клон
                        llvm::outs() << "  In root " << RootF->getName() << ": redirecting call to " << CalledOrigF->getName() << " -> " << TargetClone->getName() << "\n";

                        Call->setCalledFunction(TargetClone); // Перенаправляем вызов на клон
                        Changed = true;
                    }
                }
            }

            // Модифицируем Add/Sub внутри оригинальных корневых функций и сами корневые функции
            llvm::outs() << "SwapAddSubPass: Swapping Add/Sub within root functions...\n";

            for (Function *RootF : rootFunctions) {
                llvm::outs() << "  Processing root: " << RootF->getName() << "\n";
                if (swapBinaryAddSubInFunction(*RootF)) { // Заменяем операции во вспомогательной функции

                    Changed = true;
                }
            }

            llvm::outs() << "SwapAddSubPass: Finished.\n";
            return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
        }

        static bool isRequired() { return true; }
    };

}

// Регистрация пасса
extern "C" ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION,
        "SwapAddSub",
        LLVM_VERSION_STRING,
        [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement> Params) {

                    if (Name == "swap-add-sub")
                    {
                        MPM.addPass(SwapAddSubPass());
                        return true;
                    }
                    return false;
                });
        }};
}
