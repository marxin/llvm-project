//===--------- JITLinkGeneric.cpp - Generic JIT linker utilities ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic JITLinker utility class.
//
//===----------------------------------------------------------------------===//

#include "JITLinkGeneric.h"

#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/MemoryBuffer.h"

#define DEBUG_TYPE "jitlink"

namespace llvm {
namespace jitlink {

JITLinkerBase::~JITLinkerBase() {}

void JITLinkerBase::linkPhase1(std::unique_ptr<JITLinkerBase> Self) {

  // Build the link graph.
  if (auto GraphOrErr = buildGraph(Ctx->getObjectBuffer()))
    G = std::move(*GraphOrErr);
  else
    return Ctx->notifyFailed(GraphOrErr.takeError());
  assert(G && "Graph should have been created by buildGraph above");

  // Prune and optimize the graph.
  if (auto Err = runPasses(Passes.PrePrunePasses))
    return Ctx->notifyFailed(std::move(Err));

  LLVM_DEBUG({
    dbgs() << "Link graph \"" << G->getName() << "\" pre-pruning:\n";
    dumpGraph(dbgs());
  });

  prune(*G);

  LLVM_DEBUG({
    dbgs() << "Link graph \"" << G->getName() << "\" post-pruning:\n";
    dumpGraph(dbgs());
  });

  // Run post-pruning passes.
  if (auto Err = runPasses(Passes.PostPrunePasses))
    return Ctx->notifyFailed(std::move(Err));

  // Sort blocks into segments.
  auto Layout = layOutBlocks();

  // Allocate memory for segments.
  if (auto Err = allocateSegments(Layout))
    return Ctx->notifyFailed(std::move(Err));

  // Notify client that the defined symbols have been assigned addresses.
  Ctx->notifyResolved(*G);

  auto ExternalSymbols = getExternalSymbolNames();

  // We're about to hand off ownership of ourself to the continuation. Grab a
  // pointer to the context so that we can call it to initiate the lookup.
  //
  // FIXME: Once callee expressions are defined to be sequenced before argument
  // expressions (c++17) we can simplify all this to:
  //
  // Ctx->lookup(std::move(UnresolvedExternals),
  //             [Self=std::move(Self)](Expected<AsyncLookupResult> Result) {
  //               Self->linkPhase2(std::move(Self), std::move(Result));
  //             });
  auto *TmpCtx = Ctx.get();
  TmpCtx->lookup(std::move(ExternalSymbols),
                 createLookupContinuation(
                     [S = std::move(Self), L = std::move(Layout)](
                         Expected<AsyncLookupResult> LookupResult) mutable {
                       auto &TmpSelf = *S;
                       TmpSelf.linkPhase2(std::move(S), std::move(LookupResult),
                                          std::move(L));
                     }));
}

void JITLinkerBase::linkPhase2(std::unique_ptr<JITLinkerBase> Self,
                               Expected<AsyncLookupResult> LR,
                               SegmentLayoutMap Layout) {
  // If the lookup failed, bail out.
  if (!LR)
    return deallocateAndBailOut(LR.takeError());

  // Assign addresses to external addressables.
  applyLookupResult(*LR);

  LLVM_DEBUG({
    dbgs() << "Link graph \"" << G->getName() << "\" before copy-and-fixup:\n";
    dumpGraph(dbgs());
  });

  // Copy block content to working memory and fix up.
  if (auto Err = copyAndFixUpBlocks(Layout, *Alloc))
    return deallocateAndBailOut(std::move(Err));

  LLVM_DEBUG({
    dbgs() << "Link graph \"" << G->getName() << "\" after copy-and-fixup:\n";
    dumpGraph(dbgs());
  });

  if (auto Err = runPasses(Passes.PostFixupPasses))
    return deallocateAndBailOut(std::move(Err));

  // FIXME: Use move capture once we have c++14.
  auto *UnownedSelf = Self.release();
  auto Phase3Continuation = [UnownedSelf](Error Err) {
    std::unique_ptr<JITLinkerBase> Self(UnownedSelf);
    UnownedSelf->linkPhase3(std::move(Self), std::move(Err));
  };

  Alloc->finalizeAsync(std::move(Phase3Continuation));
}

void JITLinkerBase::linkPhase3(std::unique_ptr<JITLinkerBase> Self, Error Err) {
  if (Err)
    return deallocateAndBailOut(std::move(Err));
  Ctx->notifyFinalized(std::move(Alloc));
}

Error JITLinkerBase::runPasses(LinkGraphPassList &Passes) {
  for (auto &P : Passes)
    if (auto Err = P(*G))
      return Err;
  return Error::success();
}

JITLinkerBase::SegmentLayoutMap JITLinkerBase::layOutBlocks() {

  SegmentLayoutMap Layout;

  /// Partition blocks based on permissions and content vs. zero-fill.
  for (auto *B : G->blocks()) {
    auto &SegLists = Layout[B->getSection().getProtectionFlags()];
    if (!B->isZeroFill())
      SegLists.ContentBlocks.push_back(B);
    else
      SegLists.ZeroFillBlocks.push_back(B);
  }

  /// Sort blocks within each list.
  for (auto &KV : Layout) {

    auto CompareBlocks = [](const Block *LHS, const Block *RHS) {
      // Sort by section, address and size
      if (LHS->getSection().getOrdinal() != RHS->getSection().getOrdinal())
        return LHS->getSection().getOrdinal() < RHS->getSection().getOrdinal();
      if (LHS->getAddress() != RHS->getAddress())
        return LHS->getAddress() < RHS->getAddress();
      return LHS->getSize() < RHS->getSize();
    };

    auto &SegLists = KV.second;
    llvm::sort(SegLists.ContentBlocks, CompareBlocks);
    llvm::sort(SegLists.ZeroFillBlocks, CompareBlocks);
  }

  LLVM_DEBUG({
    dbgs() << "Segment ordering:\n";
    for (auto &KV : Layout) {
      dbgs() << "  Segment "
             << static_cast<sys::Memory::ProtectionFlags>(KV.first) << ":\n";
      auto &SL = KV.second;
      for (auto &SIEntry :
           {std::make_pair(&SL.ContentBlocks, "content block"),
            std::make_pair(&SL.ZeroFillBlocks, "zero-fill block")}) {
        dbgs() << "    " << SIEntry.second << ":\n";
        for (auto *B : *SIEntry.first)
          dbgs() << "      " << *B << "\n";
      }
    }
  });

  return Layout;
}

Error JITLinkerBase::allocateSegments(const SegmentLayoutMap &Layout) {

  // Compute segment sizes and allocate memory.
  LLVM_DEBUG(dbgs() << "JIT linker requesting: { ");
  JITLinkMemoryManager::SegmentsRequestMap Segments;
  for (auto &KV : Layout) {
    auto &Prot = KV.first;
    auto &SegLists = KV.second;

    uint64_t SegAlign = 1;

    // Calculate segment content size.
    size_t SegContentSize = 0;
    for (auto *B : SegLists.ContentBlocks) {
      SegAlign = std::max(SegAlign, B->getAlignment());
      SegContentSize = alignToBlock(SegContentSize, *B);
      SegContentSize += B->getSize();
    }

    uint64_t SegZeroFillStart = SegContentSize;
    uint64_t SegZeroFillEnd = SegZeroFillStart;

    for (auto *B : SegLists.ZeroFillBlocks) {
      SegAlign = std::max(SegAlign, B->getAlignment());
      SegZeroFillEnd = alignToBlock(SegZeroFillEnd, *B);
      SegZeroFillEnd += B->getSize();
    }

    Segments[Prot] = {SegAlign, SegContentSize,
                      SegZeroFillEnd - SegZeroFillStart};

    LLVM_DEBUG({
      dbgs() << (&KV == &*Layout.begin() ? "" : "; ")
             << static_cast<sys::Memory::ProtectionFlags>(Prot)
             << ": alignment = " << SegAlign
             << ", content size = " << SegContentSize
             << ", zero-fill size = " << (SegZeroFillEnd - SegZeroFillStart);
    });
  }
  LLVM_DEBUG(dbgs() << " }\n");

  if (auto AllocOrErr = Ctx->getMemoryManager().allocate(Segments))
    Alloc = std::move(*AllocOrErr);
  else
    return AllocOrErr.takeError();

  LLVM_DEBUG({
    dbgs() << "JIT linker got working memory:\n";
    for (auto &KV : Layout) {
      auto Prot = static_cast<sys::Memory::ProtectionFlags>(KV.first);
      dbgs() << "  " << Prot << ": "
             << (const void *)Alloc->getWorkingMemory(Prot).data() << "\n";
    }
  });

  // Update block target addresses.
  for (auto &KV : Layout) {
    auto &Prot = KV.first;
    auto &SL = KV.second;

    JITTargetAddress NextBlockAddr =
        Alloc->getTargetMemory(static_cast<sys::Memory::ProtectionFlags>(Prot));

    for (auto *SIList : {&SL.ContentBlocks, &SL.ZeroFillBlocks})
      for (auto *B : *SIList) {
        NextBlockAddr = alignToBlock(NextBlockAddr, *B);
        B->setAddress(NextBlockAddr);
        NextBlockAddr += B->getSize();
      }
  }

  return Error::success();
}

DenseSet<StringRef> JITLinkerBase::getExternalSymbolNames() const {
  // Identify unresolved external symbols.
  DenseSet<StringRef> UnresolvedExternals;
  for (auto *Sym : G->external_symbols()) {
    assert(Sym->getAddress() == 0 &&
           "External has already been assigned an address");
    assert(Sym->getName() != StringRef() && Sym->getName() != "" &&
           "Externals must be named");
    UnresolvedExternals.insert(Sym->getName());
  }
  return UnresolvedExternals;
}

void JITLinkerBase::applyLookupResult(AsyncLookupResult Result) {
  for (auto *Sym : G->external_symbols()) {
    assert(Sym->getAddress() == 0 && "Symbol already resolved");
    assert(!Sym->isDefined() && "Symbol being resolved is already defined");
    assert(Result.count(Sym->getName()) && "Missing resolution for symbol");
    Sym->getAddressable().setAddress(Result[Sym->getName()].getAddress());
  }

  LLVM_DEBUG({
    dbgs() << "Externals after applying lookup result:\n";
    for (auto *Sym : G->external_symbols())
      dbgs() << "  " << Sym->getName() << ": "
             << formatv("{0:x16}", Sym->getAddress()) << "\n";
  });
  assert(llvm::all_of(G->external_symbols(),
                      [](Symbol *Sym) { return Sym->getAddress() != 0; }) &&
         "All symbols should have been resolved by this point");
}

void JITLinkerBase::deallocateAndBailOut(Error Err) {
  assert(Err && "Should not be bailing out on success value");
  assert(Alloc && "can not call deallocateAndBailOut before allocation");
  Ctx->notifyFailed(joinErrors(std::move(Err), Alloc->deallocate()));
}

void JITLinkerBase::dumpGraph(raw_ostream &OS) {
  assert(G && "Graph is not set yet");
  G->dump(dbgs(), [this](Edge::Kind K) { return getEdgeKindName(K); });
}

void prune(LinkGraph &G) {
  std::vector<Symbol *> Worklist;
  DenseSet<Block *> VisitedBlocks;

  // Build the initial worklist from all symbols initially live.
  for (auto *Sym : G.defined_symbols())
    if (Sym->isLive())
      Worklist.push_back(Sym);

  // Propagate live flags to all symbols reachable from the initial live set.
  while (!Worklist.empty()) {
    auto *Sym = Worklist.back();
    Worklist.pop_back();

    auto &B = Sym->getBlock();

    // Skip addressables that we've visited before.
    if (VisitedBlocks.count(&B))
      continue;

    VisitedBlocks.insert(&B);

    for (auto &E : Sym->getBlock().edges()) {
      if (E.getTarget().isDefined() && !E.getTarget().isLive()) {
        E.getTarget().setLive(true);
        Worklist.push_back(&E.getTarget());
      }
    }
  }

  // Collect all the symbols to remove, then remove them.
  {
    LLVM_DEBUG(dbgs() << "Dead-stripping symbols:\n");
    std::vector<Symbol *> SymbolsToRemove;
    for (auto *Sym : G.defined_symbols())
      if (!Sym->isLive())
        SymbolsToRemove.push_back(Sym);
    for (auto *Sym : SymbolsToRemove) {
      LLVM_DEBUG(dbgs() << "  " << *Sym << "...\n");
      G.removeDefinedSymbol(*Sym);
    }
  }

  // Delete any unused blocks.
  {
    LLVM_DEBUG(dbgs() << "Dead-stripping blocks:\n");
    std::vector<Block *> BlocksToRemove;
    for (auto *B : G.blocks())
      if (!VisitedBlocks.count(B))
        BlocksToRemove.push_back(B);
    for (auto *B : BlocksToRemove) {
      LLVM_DEBUG(dbgs() << "  " << *B << "...\n");
      G.removeBlock(*B);
    }
  }
}

} // end namespace jitlink
} // end namespace llvm
