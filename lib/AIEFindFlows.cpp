// (c) Copyright 2019 Xilinx Inc. All Rights Reserved.

#include "mlir/IR/Attributes.h"
#include "mlir/IR/BlockAndValueMapping.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"
#include "AIEDialect.h"

#define DEBUG_TYPE "aie-find-flows"

using namespace mlir;
using namespace xilinx;
using namespace xilinx::AIE;

std::pair<WireBundle, int> getBundleForEnum(SlavePortEnum slave) {
  switch(slave) {
  case SlavePortEnum::ME0: return std::make_pair(WireBundle::ME, 0);
  case SlavePortEnum::ME1: return std::make_pair(WireBundle::ME, 1);
  case SlavePortEnum::DMA0: return std::make_pair(WireBundle::DMA, 0);
  case SlavePortEnum::DMA1: return std::make_pair(WireBundle::DMA, 1);
  case SlavePortEnum::FIFO0: return std::make_pair(WireBundle::FIFO, 0);
  case SlavePortEnum::FIFO1: return std::make_pair(WireBundle::FIFO, 1);
  case SlavePortEnum::South0: return std::make_pair(WireBundle::South, 0);
  case SlavePortEnum::South1: return std::make_pair(WireBundle::South, 1);
  case SlavePortEnum::South2: return std::make_pair(WireBundle::South, 2);
  case SlavePortEnum::South3: return std::make_pair(WireBundle::South, 3);
  case SlavePortEnum::South4: return std::make_pair(WireBundle::South, 4);
  case SlavePortEnum::South5: return std::make_pair(WireBundle::South, 5);
  case SlavePortEnum::West0: return std::make_pair(WireBundle::West, 0);
  case SlavePortEnum::West1: return std::make_pair(WireBundle::West, 1);
  case SlavePortEnum::West2: return std::make_pair(WireBundle::West, 2);
  case SlavePortEnum::West3: return std::make_pair(WireBundle::West, 3);
  case SlavePortEnum::North0: return std::make_pair(WireBundle::North, 0);
  case SlavePortEnum::North1: return std::make_pair(WireBundle::North, 1);
  case SlavePortEnum::North2: return std::make_pair(WireBundle::North, 2);
  case SlavePortEnum::North3: return std::make_pair(WireBundle::North, 3);
  case SlavePortEnum::East0: return std::make_pair(WireBundle::East, 0);
  case SlavePortEnum::East1: return std::make_pair(WireBundle::East, 1);
  case SlavePortEnum::East2: return std::make_pair(WireBundle::East, 2);
  case SlavePortEnum::East3: return std::make_pair(WireBundle::East, 3);
  default: llvm_unreachable("Unimplemented");
  }
}

std::pair<WireBundle, int> getBundleForEnum(MasterPortEnum master) {
  switch(master) {
  case MasterPortEnum::ME0: return std::make_pair(WireBundle::ME, 0);
  case MasterPortEnum::ME1: return std::make_pair(WireBundle::ME, 1);
  case MasterPortEnum::DMA0: return std::make_pair(WireBundle::DMA, 0);
  case MasterPortEnum::DMA1: return std::make_pair(WireBundle::DMA, 1);
  case MasterPortEnum::FIFO0: return std::make_pair(WireBundle::FIFO, 0);
  case MasterPortEnum::FIFO1: return std::make_pair(WireBundle::FIFO, 1);
  case MasterPortEnum::South0: return std::make_pair(WireBundle::South, 0);
  case MasterPortEnum::South1: return std::make_pair(WireBundle::South, 1);
  case MasterPortEnum::South2: return std::make_pair(WireBundle::South, 2);
  case MasterPortEnum::South3: return std::make_pair(WireBundle::South, 3);
  case MasterPortEnum::West0: return std::make_pair(WireBundle::West, 0);
  case MasterPortEnum::West1: return std::make_pair(WireBundle::West, 1);
  case MasterPortEnum::West2: return std::make_pair(WireBundle::West, 2);
  case MasterPortEnum::West3: return std::make_pair(WireBundle::West, 3);
  case MasterPortEnum::North0: return std::make_pair(WireBundle::North, 0);
  case MasterPortEnum::North1: return std::make_pair(WireBundle::North, 1);
  case MasterPortEnum::North2: return std::make_pair(WireBundle::North, 2);
  case MasterPortEnum::North3: return std::make_pair(WireBundle::North, 3);
  case MasterPortEnum::North4: return std::make_pair(WireBundle::North, 4);
  case MasterPortEnum::North5: return std::make_pair(WireBundle::North, 5);
  case MasterPortEnum::East0: return std::make_pair(WireBundle::East, 0);
  case MasterPortEnum::East1: return std::make_pair(WireBundle::East, 1);
  case MasterPortEnum::East2: return std::make_pair(WireBundle::East, 2);
  case MasterPortEnum::East3: return std::make_pair(WireBundle::East, 3);
  default: llvm_unreachable("Unimplemented");
  }
}

typedef std::pair<int, int> MaskValue;
typedef std::pair<Operation *, Port> PortConnection;
typedef std::pair<Port, MaskValue> PortMaskValue;
typedef std::pair<PortConnection, MaskValue> PacketConnection;

class ConnectivityAnalysis {
  ModuleOp &module;

public:
  ConnectivityAnalysis(ModuleOp &m) : module(m) {}

private:
  llvm::Optional<PortConnection>
  getConnectionThroughWire(Operation *op,
                           Port masterPort) const {
    LLVM_DEBUG(llvm::dbgs() << "Wire:" << *op << " " << stringifyWireBundle(masterPort.first) << " " << masterPort.second << "\n");
    for (auto wireOp : module.getOps<WireOp>()) {
      if(wireOp.source().getDefiningOp() == op &&
         wireOp.sourceBundle() == masterPort.first) {
        Operation *other = wireOp.dest().getDefiningOp();
        Port otherPort = std::make_pair(wireOp.destBundle(),
                                        masterPort.second);
        LLVM_DEBUG(llvm::dbgs() << "To:" << *other << " "
                                << stringifyWireBundle(otherPort.first) << " "
                                << otherPort.second << "\n");
        return std::make_pair(other, otherPort);
      }
      if(wireOp.dest().getDefiningOp() == op &&
         wireOp.destBundle() == masterPort.first) {
        Operation *other = wireOp.source().getDefiningOp();
        Port otherPort = std::make_pair(wireOp.sourceBundle(),
                                        masterPort.second);
        LLVM_DEBUG(llvm::dbgs() << "From:" << *other << " "
                                << stringifyWireBundle(otherPort.first) << " "
                                << otherPort.second << "\n");
        return std::make_pair(other, otherPort);
      }
    }
    return None;
  }

  std::vector<PortMaskValue>
  getConnectionsThroughSwitchbox(SwitchboxOp op,
                                       Port sourcePort) const {
    LLVM_DEBUG(llvm::dbgs() << "Switchbox:\n");
    Region &r = op.connections();
    Block &b = r.front();
    std::vector<PortMaskValue> portSet;
    for (auto connectOp : b.getOps<ConnectOp>()) {
      if(connectOp.sourcePort() == sourcePort) {
        MaskValue maskValue = std::make_pair(0, 0);
        portSet.push_back(std::make_pair(connectOp.destPort(), maskValue));
        LLVM_DEBUG(llvm::dbgs() << "To:" << stringifyWireBundle(connectOp.destPort().first) << " " << connectOp.destPort().second << "\n");
      }
    }
    for (auto connectOp : b.getOps<PacketRulesOp>()) {
      if (connectOp.sourcePort() == sourcePort) {
        LLVM_DEBUG(llvm::dbgs() << "Packet From: " << stringifyWireBundle(connectOp.sourcePort().first) << " " << (int)sourcePort.first << "\n");
        for (auto masterSetOp : b.getOps<MasterSetOp>())
          for (Value amsel : masterSetOp.amsels())
            for (auto ruleOp : connectOp.rules().front().getOps<PacketRuleOp>()) {
               if (ruleOp.amsel() == amsel) {
                LLVM_DEBUG(llvm::dbgs() << "To:" << stringifyWireBundle(masterSetOp.destPort().first) << " " << masterSetOp.destPort().second << "\n");
                MaskValue maskValue = std::make_pair(ruleOp.maskInt(), ruleOp.valueInt());
                portSet.push_back(std::make_pair(masterSetOp.destPort(), maskValue));
              }
            }
      }
    }
    return portSet;
  }

public:
  // Get the tiles connected to the given tile, starting from the given
  // output port of the tile.  This is 1:N relationship because each
  // switchbox can broadcast.
  std::vector<PacketConnection>
  getConnectedTiles(TileOp tileOp,
                    Port port) const {
    // The accumulated result;
    std::vector<PacketConnection> connectedTiles;
    // A worklist of PortConnections to visit.  These are all input ports of
    // some object (likely either a TileOp or a SwitchboxOp).
    std::vector<PacketConnection> worklist;
    // Start the worklist by traversing from the tile to its connected
    // switchbox.
    auto t = getConnectionThroughWire(tileOp.getOperation(), port);

    // If there is no wire to traverse, then just return no connection
    if(!t.hasValue()) return connectedTiles;
    worklist.push_back(std::make_pair(t.getValue(),
                                      std::make_pair(0, 0)));

    while(!worklist.empty()) {
      PacketConnection t = worklist.back();
      worklist.pop_back();
      PortConnection portConnection = t.first;
      MaskValue maskValue = t.second;
      Operation *other = portConnection.first;
      Port otherPort = portConnection.second;
      if(auto tileOp = dyn_cast_or_null<TileOp>(other)) {
        // If we got to a tile, then add it to the result.
        connectedTiles.push_back(t);
      } else if(auto switchOp = dyn_cast_or_null<SwitchboxOp>(other)) {
        std::vector<PortMaskValue> nextPortMaskValues = getConnectionsThroughSwitchbox(switchOp, otherPort);
        bool matched = false;
        for(auto &nextPortMaskValue: nextPortMaskValues) {
          Port nextPort = nextPortMaskValue.first;
          MaskValue nextMaskValue = nextPortMaskValue.second;
          int maskConflicts = nextMaskValue.first & maskValue.first;
          LLVM_DEBUG(llvm::dbgs() << "Mask: " << maskValue.first << " " << maskValue.second << "\n");
          LLVM_DEBUG(llvm::dbgs() << "NextMask: " << nextMaskValue.first << " " << nextMaskValue.second << "\n");
          LLVM_DEBUG(llvm::dbgs() << maskConflicts << "\n");

          if((maskConflicts & nextMaskValue.second) !=
             (maskConflicts & maskValue.second)) {
            // Incoming packets cannot match this rule. Skip it.
            continue;
          }
          matched = true;
          MaskValue newMaskValue =
            std::make_pair(maskValue.first | nextMaskValue.first,
                           maskValue.second | (nextMaskValue.first & nextMaskValue.second));
          auto nextConnection =
            getConnectionThroughWire(switchOp, nextPort);

          // If there is no wire to follow then bail out.
          if(!nextConnection.hasValue()) continue;

          worklist.push_back(std::make_pair(nextConnection.getValue(),
                                            newMaskValue));
        }
        if(nextPortMaskValues.size() > 0 && !matched) {
          // No rule matched some incoming packet.  This is likely a
          // configuration error.
          LLVM_DEBUG(llvm::dbgs() << "No rule matched incoming packet here: ");
          other->dump();
        }
      }
    }
    return connectedTiles;
  }
};

struct StartFlow : public OpConversionPattern<AIE::TileOp> {
  using OpConversionPattern<AIE::TileOp>::OpConversionPattern;
  ConnectivityAnalysis analysis;
  ModuleOp &module;
  StartFlow(MLIRContext *context, ModuleOp &m, ConnectivityAnalysis a,
            PatternBenefit benefit = 1)
      : OpConversionPattern<TileOp>(context, benefit),
    module(m), analysis(a) {}

  LogicalResult match(Operation *op) const override {
    return success();
  }

  void rewrite(AIE::TileOp op, ArrayRef<Value > operands,
                  ConversionPatternRewriter &rewriter) const override {
    Operation *Op = op.getOperation();
    Operation *newOp = rewriter.clone(*Op);
    newOp->setAttr("HasFlow", BoolAttr::get(true, rewriter.getContext()));
    rewriter.replaceOp(Op, newOp->getOpResults());

    rewriter.setInsertionPoint(Op->getBlock()->getTerminator());

    std::vector<WireBundle> bundles = {WireBundle::ME, WireBundle::DMA};
    for(WireBundle bundle: bundles) {
      for(int i = 0; i < op.getNumSourceConnections(bundle); i++) {
        std::vector<PacketConnection> tiles =
          analysis.getConnectedTiles(op,
                                     std::make_pair(bundle, i));
        for(PacketConnection &c: tiles) {
          PortConnection portConnection = c.first;
          MaskValue maskValue = c.second;
          Operation *destOp = portConnection.first;
          Port destPort = portConnection.second;
          IntegerType i32 = IntegerType::get(32, rewriter.getContext());
          if(maskValue.first == 0) {
            Operation *flowOp = rewriter.create<FlowOp>(Op->getLoc(),
                                                        newOp->getResult(0),
                                                        (int)bundle,
                                                        (int)i,
                                                        destOp->getResult(0),
                                                        (int)destPort.first,
                                                        (int)destPort.second);
          } else {
            PacketFlowOp flowOp = rewriter.create<PacketFlowOp>(Op->getLoc(),
                                                       maskValue.second);
            flowOp.ensureTerminator(flowOp.ports(),
                                    rewriter,
                                    Op->getLoc());
            OpBuilder::InsertPoint ip = rewriter.saveInsertionPoint();
            rewriter.setInsertionPoint(flowOp.ports().front().getTerminator());
            Operation *packetSourceOp = rewriter.create<PacketSourceOp>(Op->getLoc(),
                                                            newOp->getResult(0),
                                                                 bundle,
                                                                 (int)i);
            Operation *packetDestOp = rewriter.create<PacketDestOp>(Op->getLoc(),
                                                        destOp->getResult(0),
                                                        destPort.first,
                                                        (int)destPort.second);
            rewriter.restoreInsertionPoint(ip);
          }
        }
      }
    }
  }
};


struct AIEFindFlowsPass : public PassWrapper<AIEFindFlowsPass,
                                             OperationPass<ModuleOp>> {
  void getDependentDialects(::mlir::DialectRegistry &registry) const override {  
    registry.insert<StandardOpsDialect>();
    registry.insert<xilinx::AIE::AIEDialect>();
  }
  void runOnOperation() override {

    ModuleOp m = getOperation();
    ConnectivityAnalysis analysis(m);

    ConversionTarget target(getContext());
    target.addLegalOp<FlowOp>();
    target.addLegalOp<PacketFlowOp>();
    target.addLegalOp<PacketSourceOp>();
    target.addLegalOp<PacketDestOp>();
    target.addLegalOp<EndOp>();
    target.addDynamicallyLegalOp<TileOp>([](TileOp op) { return (bool)op.getOperation()->getAttrOfType<BoolAttr>("HasFlow"); });
    //   target.addDynamicallyLegalDialect<AIEDialect>();
    LLVM_DEBUG(llvm::dbgs() << "Starting Find Flows\n");
    OwningRewritePatternList patterns;
    patterns.insert<StartFlow>(m.getContext(), m, analysis);
    if (failed(applyPartialConversion(m, target, patterns)))
      signalPassFailure();
    return;
  }
};

void xilinx::AIE::registerAIEFindFlowsPass() {
    PassRegistration<AIEFindFlowsPass>(
      "aie-find-flows",
      "Extract flows from a placed and routed design");
}
