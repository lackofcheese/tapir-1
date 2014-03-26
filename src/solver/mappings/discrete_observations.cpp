#include "discrete_observations.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <sstream>
#include <vector>

#include "solver/BeliefNode.hpp"
#include "solver/BeliefTree.hpp"
#include "solver/abstract-problem/Model.hpp"

#include "solver/abstract-problem/Observation.hpp"

#include "ActionMapping.hpp"
#include "ActionPool.hpp"
#include "ObservationPool.hpp"
#include "ObservationMapping.hpp"

#include "global.hpp"

namespace solver {
/* ------------------- ModelWithDiscreteObservations ------------------- */
std::unique_ptr<ObservationPool> ModelWithDiscreteObservations::createObservationPool() {
    return std::make_unique<DiscreteObservationPool>();
}

/* --------------------- DiscreteObservationPool --------------------- */
std::unique_ptr<ObservationMapping> DiscreteObservationPool::createObservationMapping() {
    return std::make_unique<DiscreteObservationMap>(actionPool_);
}

/* ---------------------- DiscreteObservationMap ---------------------- */
DiscreteObservationMap::DiscreteObservationMap(ActionPool *actionPool) :
        actionPool_(actionPool),
        childMap_(),
        totalVisitCount_(0) {
}

BeliefNode* DiscreteObservationMap::getBelief(Observation const &obs) const {
    try {
        return childMap_.at(obs.copy()).childNode.get();
    } catch (const std::out_of_range &oor) {
        return nullptr;
    }
}

BeliefNode* DiscreteObservationMap::createBelief(const Observation& obs) {
    std::unique_ptr<Observation> observation = obs.copy();
    std::unique_ptr<BeliefNode> node = std::make_unique<BeliefNode>(
            actionPool_->createActionMapping());
    BeliefNode *nodePtr = node.get();
    DiscreteObservationMapEntry entry;
    entry.childNode = std::move(node);
    childMap_.emplace(std::move(observation), std::move(entry));
    return nodePtr;
}

long DiscreteObservationMap::getNChildren() const {
    return childMap_.size();
}

void DiscreteObservationMap::updateVisitCount(Observation const &obs,
        long deltaNVisits) {
    childMap_[obs.copy()].visitCount += deltaNVisits;
    totalVisitCount_ += deltaNVisits;
}
long DiscreteObservationMap::getVisitCount(Observation const &obs) const {
    return childMap_.at(obs.copy()).visitCount;
}
long DiscreteObservationMap::getTotalVisitCount() const {
    return totalVisitCount_;
}

/* ------------------ DiscreteObservationTextSerializer ------------------ */
void DiscreteObservationTextSerializer::saveObservationPool(
        ObservationPool const &/*observationPool*/, std::ostream &/*os*/) {
    // We won't bother writing the pool to file as the model can make a new one.
}

std::unique_ptr<ObservationPool> DiscreteObservationTextSerializer::loadObservationPool(
        std::istream &/*is*/) {
    // Here we just create a new one.
    return solver_->getModel()->createObservationPool();
}

void DiscreteObservationTextSerializer::saveObservationMapping(
        ObservationMapping const &map, std::ostream &os) {
    DiscreteObservationMap const &discMap =
            (static_cast<DiscreteObservationMap const &>(map));
    os << discMap.getNChildren() << " observation children; ";
    os << discMap.getTotalVisitCount() << " visits {" << std::endl;
    std::vector<std::string> lines;
    for (DiscreteObservationMap::ChildMap::value_type const &entry : discMap.childMap_) {
        std::ostringstream sstr;
        sstr << "\t";
        saveObservation(entry.first.get(), sstr);
        sstr << " -> NODE " << entry.second.childNode->getId();
        sstr << "; " << entry.second.visitCount << " visits";
        sstr << std::endl;
        lines.push_back(sstr.str());
    }
    std::sort(lines.begin(), lines.end());
    for (std::string line : lines) {
        os << line;
    }
    os << "}" << std::endl;
}

std::unique_ptr<ObservationMapping> DiscreteObservationTextSerializer::loadObservationMapping(
        std::istream &is) {
    std::unique_ptr<ObservationMapping> map(
            solver_->getObservationPool()->createObservationMapping());
    DiscreteObservationMap &discMap =
                    (static_cast<DiscreteObservationMap &>(*map));
    std::string line;
    std::getline(is, line);
    std::string tmpStr;
    std::istringstream totalsStream(line);
    long nChildren;
    totalsStream >> nChildren >> tmpStr >> tmpStr;
    totalsStream >> discMap.totalVisitCount_;

    for (int i = 0; i < nChildren; i++) {
        std::getline(is, line);
        std::istringstream entryStream(line);
        std::unique_ptr<Observation> obs = loadObservation(entryStream);

        entryStream >> tmpStr >> tmpStr;
        std::getline(entryStream, tmpStr, ';');
        long childId;
        std::istringstream(tmpStr) >> childId;
        long visitCount;
        entryStream >> visitCount;

        // Create the child node and set its values correctly.
        BeliefNode *node = map->createBelief(*obs);
        solver_->getPolicy()->setNode(childId, node);
        discMap.childMap_[obs->copy()].visitCount = visitCount;
    }
    // Read the last line for the closing brace.
    std::getline(is, line);
    return std::move(map);
}
} /* namespace solver */

