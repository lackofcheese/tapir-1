#include "ActionNode.hpp"

#include <memory>                       // for unique_ptr
#include <utility>                      // for make_pair, move, pair
#include <vector>                       // for vector

#include "global.hpp"                     // for make_unique

#include "BeliefNode.hpp"

#include "abstract-problem/Action.hpp"                   // for Action
#include "abstract-problem/Observation.hpp"              // for Observation
#include "mappings/ObservationMapping.hpp"       // for ObservationMapping

namespace solver {
ActionNode::ActionNode() :
    ActionNode(nullptr) {
}

ActionNode::ActionNode(std::unique_ptr<ObservationMapping> mapping) :
    nParticles_(0),
    totalQValue_(0),
    meanQValue_(-std::numeric_limits<double>::infinity()),
    obsMap_(std::move(mapping)) {
}

// Default destructor
ActionNode::~ActionNode() {
}

void ActionNode::changeTotalQValue(double deltaQ, long deltaNParticles) {
    totalQValue_ += deltaQ;
    nParticles_ += deltaNParticles;
    recalculateQValue();
}

void ActionNode::updateSequenceCount(Observation const &observation,
        double discountFactor, long deltaNParticles) {
    BeliefNode *childBelief = getChild(observation);

    long newSequenceCount = childBelief->getNParticles();
    newSequenceCount -= childBelief->numberOfStartingSequences_;
    newSequenceCount += childBelief->numberOfEndingSequences_;
    long oldSequenceCount = newSequenceCount - deltaNParticles;

    double oldChildQ = childBelief->getQValue();
    childBelief->recalculateQValue();
    double newChildQ = childBelief->getQValue();

    if (oldSequenceCount != 0) {
        totalQValue_ -= oldSequenceCount * discountFactor * oldChildQ;
    }
    if (newSequenceCount != 0) {
        totalQValue_ += newSequenceCount * discountFactor * newChildQ;
    }
    nParticles_ += deltaNParticles;
    recalculateQValue();
}

void ActionNode::recalculateQValue() {
    if (nParticles_ > 0) {
        meanQValue_ = totalQValue_ / nParticles_;
    } else {
        totalQValue_ = 0;
        meanQValue_ = -std::numeric_limits<double>::infinity();
    }
}

long ActionNode::getNParticles() const {
    return nParticles_;
}

double ActionNode::getTotalQValue () const {
    return totalQValue_;
}

double ActionNode::getQValue () const {
    return meanQValue_;
}

ObservationMapping *ActionNode::getMapping() {
    return obsMap_.get();
}

BeliefNode *ActionNode::getChild(Observation const &obs) const {
    return obsMap_->getBelief(obs);
}

std::pair<BeliefNode *, bool> ActionNode::createOrGetChild(Observation const &obs) {
    BeliefNode *beliefChild = getChild(obs);
    bool added = false;
    if (beliefChild == nullptr) {
        beliefChild = obsMap_->createBelief(obs);
        added = true;
    }
    return std::make_pair(beliefChild, added);
}
} /* namespace solver */
