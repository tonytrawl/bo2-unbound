#include "inherited_content.hpp"

#include <cassert>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {
using namespace unbound;
struct Node { InheritedEntryKind kind; std::string data; };
struct Io {
    std::map<std::string, Node> nodes;
    std::vector<std::string> writes;
    std::vector<std::string> verified;
    std::string failList, failCopy, failMkdir, deleteAfterCopy, extraAfterCopy;
    unsigned copies=0;
    InheritedStat inspect(const std::string& path) {
        const auto found=nodes.find(path);
        return found==nodes.end()?InheritedStat{}:
            InheritedStat{found->second.kind,found->second.data.size(),0};
    }
    bool listNames(const std::string& path,std::vector<std::string>& names,std::string& error) {
        if(path==failList){error="Injected enumeration failure";return false;}
        const auto prefix=path+"/";
        for(const auto& [name,node]:nodes){
            (void)node;
            if(name.rfind(prefix,0)==0&&name.find('/',prefix.size())==std::string::npos)
                names.push_back(name.substr(prefix.size()));
        }
        return true;
    }
    bool makeDirectory(const std::string& path,std::string& error) {
        if(path==failMkdir){error="Injected mkdir failure";return false;}
        assert(!nodes.count(path));nodes[path]={InheritedEntryKind::Directory,{}};
        writes.push_back("mkdir "+path);return true;
    }
    bool copyInheritedFile(const std::string& source,const std::string& target,
                           std::uint64_t bytes,bool mayReplace,std::string& error) {
        ++copies;verified.push_back(target);
        assert(nodes.at(source).kind==InheritedEntryKind::Regular&&nodes.at(source).data.size()==bytes);
        if(target==failCopy){error="Injected read/write/close/hash failure before atomic commit";return false;}
        if(!mayReplace)assert(!nodes.count(target));
        nodes[target]={InheritedEntryKind::Regular,nodes.at(source).data};writes.push_back("copy "+target);
        if(mayReplace){nodes.erase(target+".unbound-new");nodes.erase(target+".unbound-replacing");}
        if(!deleteAfterCopy.empty()&&target!=deleteAfterCopy)nodes.erase(deleteAfterCopy);
        if(!extraAfterCopy.empty()){
            nodes[extraAfterCopy]={InheritedEntryKind::Link,"unexpected reference"};extraAfterCopy.clear();
        }
        return true;
    }
};
Io fixture() {
    Io io;
    io.nodes={{"base",{InheritedEntryKind::Directory,{}}},
        {"base/zmb_common.english.sabl",{InheritedEntryKind::Regular,"retail common bank"}},
        {"base/spl_frontend.english.sabl",{InheritedEntryKind::Regular,"retail frontend bank"}},
        {"base/nested",{InheritedEntryKind::Directory,{}}},
        {"base/nested/empty.bin",{InheritedEntryKind::Regular,{}}}};
    return io;
}
InheritedTreePlan plan(Io& io,InheritedCopyMode mode=InheritedCopyMode::RepairMissing) {
    InheritedTreePlan result;std::string error;
    assert(buildInheritedTreePlan("base","target",mode,io,result,error));
    assert(io.writes.empty());return result;
}
void freshAndLegacyCoverage() {
    auto io=fixture();auto fresh=plan(io,InheritedCopyMode::FreshStage);std::string error;
    assert(fresh.files==3&&fresh.directories==2&&fresh.items.front().relative.empty());
    assert(fresh.sourceBytes==38&&fresh.copyBytes==38&&fresh.additionalBytes==38&&fresh.scratchBytes==0);
    assert(executeInheritedTreePlans(std::vector{fresh},38+8,8,io,error));
    assert(io.nodes.at("target/zmb_common.english.sabl").data=="retail common bank");
    assert(io.nodes.at("target/nested/empty.bin").data.empty());
    io=fixture();
    io.nodes["target"]={InheritedEntryKind::Directory,{}};
    io.nodes["target/zmb_nuked.english.sabl"]={InheritedEntryKind::Regular,"new Nuketown bank"};
    io.nodes["target/spl_frontend.english.sabl"]={InheritedEntryKind::Regular,"newer changed payload"};
    const auto repair=plan(io);assert(repair.copyBytes==18&&repair.additionalBytes==18);
    assert(executeInheritedTreePlan(repair,io,error));
    assert(io.nodes.at("target/spl_frontend.english.sabl").data=="newer changed payload");
    assert(io.nodes.at("target/zmb_nuked.english.sabl").data=="new Nuketown bank");
    assert(io.nodes.at("target/zmb_common.english.sabl").data=="retail common bank");
    io.writes.clear();const auto complete=plan(io);assert(complete.copyBytes==0);
    assert(executeInheritedTreePlan(complete,io,error)&&io.writes.empty());
}
void interruptedStageAndFailures() {
    auto io=fixture();io.nodes["target"]={InheritedEntryKind::Directory,{}};
    io.nodes["target/zmb_common.english.sabl"]={InheritedEntryKind::Regular,std::string(18,'x')};
    auto staged=plan(io,InheritedCopyMode::FreshStage);std::string error;
    assert(staged.scratchBytes==18);assert(executeInheritedTreePlan(staged,io,error));
    assert(io.nodes.at("target/zmb_common.english.sabl").data=="retail common bank");
    assert(std::find(io.verified.begin(),io.verified.end(),"target/zmb_common.english.sabl")!=io.verified.end());
    io=fixture();auto p=plan(io);io.failCopy="target/zmb_common.english.sabl";
    assert(!executeInheritedTreePlan(p,io,error));assert(!io.nodes.count(io.failCopy));
    io.failCopy.clear();io.writes.clear();p=plan(io);assert(executeInheritedTreePlan(p,io,error));
    io=fixture();p=plan(io);io.nodes.erase("base/zmb_common.english.sabl");
    assert(!executeInheritedTreePlan(p,io,error));
    io=fixture();p=plan(io);io.deleteAfterCopy="target/spl_frontend.english.sabl";
    assert(!executeInheritedTreePlan(p,io,error));assert(error.find("coverage")!=std::string::npos);
}
void boundedPlanningAndSpace() {
    auto io=fixture();auto p=plan(io);std::string error;
    assert(!executeInheritedTreePlans(std::vector{p},45,8,io,error));assert(io.writes.empty());
    InheritedTreePlan sentinel;sentinel.sourceRoot="unchanged";
    for(auto kind:{InheritedEntryKind::Link,InheritedEntryKind::Other,InheritedEntryKind::Error}){
        auto broken=fixture();broken.nodes["base/zmb_common.english.sabl"].kind=kind;
        assert(!buildInheritedTreePlan("base","target",InheritedCopyMode::RepairMissing,broken,sentinel,error));
        assert(sentinel.sourceRoot=="unchanged"&&broken.writes.empty());
    }
    io=fixture();io.nodes["target"]={InheritedEntryKind::Directory,{}};
    io.nodes["target/nested"]={InheritedEntryKind::Regular,"conflict"};
    assert(!buildInheritedTreePlan("base","target",InheritedCopyMode::RepairMissing,io,sentinel,error));
    io=fixture();io.nodes["base/../escape"]={InheritedEntryKind::Regular,"unsafe"};
    // Direct directory enumeration must reject traversal names if an IO layer
    // returns them; the native adapter additionally rejects them before return.
    struct BadNames:Io{bool listNames(const std::string&,std::vector<std::string>& n,std::string&){n={".."};return true;}} bad;
    bad.nodes["base"]={InheritedEntryKind::Directory,{}};
    assert(!buildInheritedTreePlan("base","target",InheritedCopyMode::RepairMissing,bad,sentinel,error));
    io=fixture();InheritedLimits limits;limits.maximumEntries=2;
    assert(!buildInheritedTreePlan("base","target",InheritedCopyMode::RepairMissing,io,sentinel,error,limits));
    limits={};limits.maximumFileBytes=10;
    assert(!buildInheritedTreePlan("base","target",InheritedCopyMode::RepairMissing,io,sentinel,error,limits));
    limits={};limits.maximumTotalBytes=37;
    assert(!buildInheritedTreePlan("base","target",InheritedCopyMode::RepairMissing,io,sentinel,error,limits));
    limits={};limits.maximumDepth=1;
    assert(!buildInheritedTreePlan("base","target",InheritedCopyMode::RepairMissing,io,sentinel,error,limits));
    limits={};limits.maximumPathBytes=35;
    assert(!buildInheritedTreePlan("base","target",InheritedCopyMode::RepairMissing,io,sentinel,error,limits));
    io=fixture();auto malformed=plan(io);malformed.items.back().relative="../escape";
    assert(!executeInheritedTreePlan(malformed,io,error)&&io.writes.empty());
    malformed=plan(io);malformed.items.back().mayReplaceOwnedStage=true;
    assert(!executeInheritedTreePlan(malformed,io,error)&&io.writes.empty());
}
void equivalentRootsAndOverflow() {
    InheritedTreePlan parent;parent.sourceRoot="base";parent.targetRoot="target";parent.additionalBytes=100;
    InheritedTreePlan child;child.sourceRoot="base/nested";child.targetRoot="target/nested";child.additionalBytes=50;
    std::vector<InheritedTreePlan> plans;std::string error;
    assert(addInheritedTreePlan(plans,child,error));assert(addInheritedTreePlan(plans,parent,error));
    assert(plans.size()==1&&plans.front().sourceRoot=="base");
    assert(addInheritedTreePlan(plans,child,error)&&plans.size()==1);
    child.sourceRoot="unrelated";assert(!addInheritedTreePlan(plans,child,error));
    std::uint64_t required=0;assert(inheritedRequiredSpace(plans,8,required,error)&&required==108);
    parent.additionalBytes=std::numeric_limits<std::uint64_t>::max();
    assert(!inheritedRequiredSpace(std::vector{parent},1,required,error));
}
void exactOwnedStageCoverage() {
    std::string error;
    for(const auto kind:{InheritedEntryKind::Regular,InheritedEntryKind::Directory,InheritedEntryKind::Link}){
        auto io=fixture();io.nodes["target"]={InheritedEntryKind::Directory,{}};
        io.nodes["target/unknown-stale-bank.sabl"]={kind,"stale"};
        InheritedTreePlan output;output.sourceRoot="unchanged";
        assert(!buildInheritedTreePlan("base","target",InheritedCopyMode::FreshStage,io,output,error));
        assert(io.writes.empty()&&output.sourceRoot=="unchanged");
        assert(io.nodes.count("target/unknown-stale-bank.sabl")); // No guessed cleanup/deletion.
    }
    auto io=fixture();io.nodes["target"]={InheritedEntryKind::Directory,{}};
    io.nodes["target/zmb_common.english.sabl.unbound-new"]={InheritedEntryKind::Regular,"partial"};
    io.nodes["target/zmb_common.english.sabl.unbound-replacing"]={InheritedEntryKind::Regular,"old"};
    auto staged=plan(io,InheritedCopyMode::FreshStage);
    assert(executeInheritedTreePlan(staged,io,error));
    assert(io.nodes.at("target/zmb_common.english.sabl").data=="retail common bank");
    assert(!io.nodes.count("target/zmb_common.english.sabl.unbound-new"));
    assert(!io.nodes.count("target/zmb_common.english.sabl.unbound-replacing"));
    io=fixture();staged=plan(io,InheritedCopyMode::FreshStage);
    io.nodes["target"]={InheritedEntryKind::Directory,{}};
    io.nodes["target/arrived-after-plan.sabl"]={InheritedEntryKind::Regular,"unexpected"};
    assert(!executeInheritedTreePlan(staged,io,error)&&io.writes.empty());
    io=fixture();staged=plan(io,InheritedCopyMode::FreshStage);
    io.extraAfterCopy="target/arrived-during-copy.sabl";
    assert(!executeInheritedTreePlan(staged,io,error));
    assert(error.find("unexpected owned stage entry")!=std::string::npos);
    assert(io.nodes.count("target/arrived-during-copy.sabl"));
    // Live migration deliberately preserves extra mod/user files instead.
    io=fixture();io.nodes["target"]={InheritedEntryKind::Directory,{}};
    io.nodes["target/zmb_nuked.english.sabl"]={InheritedEntryKind::Regular,"payload"};
    const auto repair=plan(io,InheritedCopyMode::RepairMissing);
    assert(executeInheritedTreePlan(repair,io,error));
    assert(io.nodes.at("target/zmb_nuked.english.sabl").data=="payload");
}
}
int main(){freshAndLegacyCoverage();interruptedStageAndFailures();boundedPlanningAndSpace();equivalentRootsAndOverflow();exactOwnedStageCoverage();
    std::cout<<"Inherited stock coverage, missing-only migration, stage reverification, bounds, space and copy-failure policy passed\n";}
