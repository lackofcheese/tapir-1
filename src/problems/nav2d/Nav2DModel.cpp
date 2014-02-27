#include "Nav2DModel.hpp"

#define _USE_MATH_DEFINES
#include <cmath>                        // for pow, floor
#include <cstddef>                      // for size_t
#include <cstdlib>                      // for exit

#include <fstream>                      // for operator<<, basic_ostream, endl, basic_ostream<>::__ostream_type, ifstream, basic_ostream::operator<<, basic_istream, basic_istream<>::__istream_type
#include <initializer_list>
#include <iostream>                     // for cout, cerr
#include <memory>                       // for unique_ptr, default_delete
#include <random>                       // for uniform_int_distribution, bernoulli_distribution
#include <set>                          // for set, _Rb_tree_const_iterator, set<>::iterator
#include <string>                       // for string, getline, char_traits, basic_string
#include <tuple>                        // for tie, tuple
#include <unordered_map>                // for unordered_map<>::value_type, unordered_map
#include <utility>                      // for move, pair, make_pair
#include <vector>                       // for vector, vector<>::reference, __alloc_traits<>::value_type, operator==

#include <boost/program_options.hpp>    // for variables_map, variable_value

#include "global.hpp"                     // for RandomGenerator, make_unique
#include "problems/shared/geometry/Point2D.hpp"
#include "problems/shared/geometry/Vector2D.hpp"
#include "problems/shared/geometry/Rectangle2D.hpp"

#include "problems/shared/ModelWithProgramOptions.hpp"  // for ModelWithProgramOptions

#include "solver/geometry/Action.hpp"            // for Action
#include "solver/geometry/Observation.hpp"       // for Observation
#include "solver/geometry/State.hpp"       // for State

#include "solver/mappings/discretized_actions.hpp"
#include "solver/mappings/approximate_observations.hpp"

#include "solver/indexing/RTree.hpp"
#include "solver/indexing/FlaggingVisitor.hpp"

#include "solver/ChangeFlags.hpp"        // for ChangeFlags
#include "solver/Model.hpp"             // for Model::StepResult, Model
#include "solver/StatePool.hpp"

#include "Nav2DAction.hpp"         // for Nav2DAction
#include "Nav2DObservation.hpp"    // for Nav2DObservation
#include "Nav2DState.hpp"          // for Nav2DState

using std::cerr;
using std::cout;
using std::endl;

using geometry::Point2D;
using geometry::Vector2D;
using geometry::Rectangle2D;
using geometry::RTree;

namespace po = boost::program_options;

namespace nav2d {
Nav2DModel::Nav2DModel(RandomGenerator *randGen,
        po::variables_map vm) :
    ModelWithProgramOptions(randGen, vm),
    timeStepLength_(vm["problem.timeStepLength"].as<double>()),
    costPerUnitTime_(vm["problem.costPerUnitTime"].as<double>()),
    interpolationStepCount_(vm["problem.interpolationStepCount"].as<double>()),
    crashPenalty_(vm["problem.crashPenalty"].as<double>()),
    goalReward_(vm["problem.goalReward"].as<double>()),
    maxSpeed_(vm["problem.maxSpeed"].as<double>()),
    costPerUnitDistance_(vm["problem.costPerUnitDistance"].as<double>()),
    speedErrorType_(parseErrorType(
                vm["problem.speedErrorType"].as<std::string>())),
    speedErrorSD_(vm["problem.speedErrorSD"].as<double>()),
    maxRotationalSpeed_(vm["problem.maxRotationalSpeed"].as<double>()),
    costPerRevolution_(vm["problem.costPerRevolution"].as<double>()),
    rotationErrorType_(parseErrorType(
                vm["problem.rotationErrorType"].as<std::string>())),
    rotationErrorSD_(vm["problem.rotationErrorSD"].as<double>()),
    maxObservationDistance_(vm["SBT.maxObservationDistance"].as<double>()),
    nStVars_(2),
    minVal_(-(crashPenalty_ + maxSpeed_ * costPerUnitDistance_
            + maxRotationalSpeed_ * costPerRevolution_)
            / (1 - getDiscountFactor())),
    maxVal_(0),
    mapArea_(),
    startAreas_(),
    totalStartArea_(0),
    observationAreas_(),
    goalAreas_(),
    obstacles_(),
    obstacleTree_(nStVars_),
    goalAreaTree_(nStVars_),
    startAreaTree_(nStVars_),
    observationAreaTree_(nStVars_),
    changes_()
         {
    // Read the map from the file.
    std::ifstream inFile;
    char const *mapPath = vm["problem.mapPath"].as<std::string>().c_str();
    inFile.open(mapPath);
    if (!inFile.is_open()) {
        std::cerr << "Failed to open " << mapPath << endl;
        exit(1);
    }
    std::string line;
    while (std::getline(inFile, line)) {
        cerr << line << endl;
        std::istringstream iss(line);
        std::string typeString;
        int64_t id;
        Rectangle2D rect;
        iss >> typeString >> id >> rect;
        AreaType areaType = parseAreaType(typeString);
        if (areaType == AreaType::WORLD) {
            mapArea_ = rect;
        } else {
            addArea(id, rect, areaType);
        }
    }
    inFile.close();

    cout << "Constructed the Nav2DModel" << endl;
    cout << "Discount: " << getDiscountFactor() << endl;
    cout << "nStVars: " << nStVars_ << endl;
    cout << "Testing random initial states:" << endl;
    for (int i = 0; i < 2; i++) {
        std::unique_ptr<solver::State> state = sampleAnInitState();
        cout << *state << " ==> " << getHeuristicValue(*state) << endl;
    }
    cout << "Testing random states:" << endl;
    for (int i = 0; i < 2; i++) {
        std::unique_ptr<solver::State> state = sampleStateUniform();
        cout << *state << " ==> " << getHeuristicValue(*state) << endl;
    }
    cout << "nParticles: " << getNParticles() << endl;
    cout << "Random state drawn:" << endl;
    drawState(*sampleAnInitState(), cout);
}

std::string Nav2DModel::areaTypeToString(Nav2DModel::AreaType type) {
    switch(type) {
    case AreaType::EMPTY:
        return "Empty";
    case AreaType::WORLD:
        return "World";
    case AreaType::START:
        return "Start";
    case AreaType::OBSERVATION:
        return "Observation";
    case AreaType::GOAL:
        return "Goal";
    case AreaType::OBSTACLE:
        return "Obstacle";
    case AreaType::OUT_OF_BOUNDS:
        return "OOB";
    default:
        cerr << "ERROR: Invalid area code: " << static_cast<long>(type);
        return "ERROR";
     }
}

Nav2DModel::AreaType Nav2DModel::parseAreaType(std::string text) {
    if (text == "World") {
        return AreaType::WORLD;
    } else if (text == "Start") {
        return AreaType::START;
    } else if (text == "Observation") {
        return AreaType::OBSERVATION;
    } else if (text == "Goal") {
        return AreaType::GOAL;
    } else if (text == "Obstacle") {
        return AreaType::OBSTACLE;
    } else if (text == "Empty") {
        return AreaType::EMPTY;
    } else if (text == "OOB") {
        return AreaType::OUT_OF_BOUNDS;
    } else {
        cerr << "ERROR: Invalid area type: " << text;
        return AreaType::EMPTY;
    }
}

Nav2DModel::ErrorType Nav2DModel::parseErrorType(std::string text) {
    if (text == "proportional gaussian noise") {
        return ErrorType::PROPORTIONAL_GAUSSIAN_NOISE;
    } else if (text == "absolute gaussian noise") {
        return ErrorType::ABSOLUTE_GAUSSIAN_NOISE;
    } else if (text == "none") {
        return ErrorType::NONE;
    } else {
        cerr << "ERROR: Invalid error type - " << text;
        return ErrorType::PROPORTIONAL_GAUSSIAN_NOISE;
    }
}

double Nav2DModel::applySpeedError(double speed) {
    switch(speedErrorType_) {
    case ErrorType::PROPORTIONAL_GAUSSIAN_NOISE:
        speed = std::normal_distribution<double>(1.0, speedErrorSD_)(
                *getRandomGenerator()) * speed;
        if (speed < 0) {
            speed = 0;
        }
        return speed;
    case ErrorType::ABSOLUTE_GAUSSIAN_NOISE:
        speed = std::normal_distribution<double>(speed, speedErrorSD_)(
                *getRandomGenerator());
        if (speed < 0) {
            speed = 0;
        }
        return speed;
    case ErrorType::NONE:
        return speed;
    default:
        cerr << "Cannot calculate speed error";
        return speed;
    }
}

double Nav2DModel::applyRotationalError(double rotationalSpeed) {
    switch(rotationErrorType_) {
    case ErrorType::PROPORTIONAL_GAUSSIAN_NOISE:
        return rotationalSpeed * std::normal_distribution<double>(
                1.0, rotationErrorSD_)(*getRandomGenerator());
    case ErrorType::ABSOLUTE_GAUSSIAN_NOISE:
        return std::normal_distribution<double>(
                rotationalSpeed, speedErrorSD_)(*getRandomGenerator());
    case ErrorType::NONE:
        return rotationalSpeed;
    default:
        cerr << "Cannot calculate rotational error";
        return rotationalSpeed;
    }
}

void Nav2DModel::addArea(int64_t id, Rectangle2D const &area,
        Nav2DModel::AreaType type) {
    getAreas(type)->emplace(id, area);
    std::vector<double> lowCorner = area.getLowerLeft().asVector();
    std::vector<double> highCorner = area.getUpperRight().asVector();
    SpatialIndex::Region region(&lowCorner[0], &highCorner[0], nStVars_);
    getTree(type)->getTree()->insertData(0, nullptr, region, id);
    if (type == AreaType::START) {
        totalStartArea_ += area.getArea();
    }
}

std::unique_ptr<Nav2DState> Nav2DModel::sampleStateAt(Point2D position) {
    return std::make_unique<Nav2DState>(position,
            -std::uniform_real_distribution<double>(-0.5, 0.5)(
                    *getRandomGenerator()),
                    costPerUnitDistance_, costPerRevolution_);
}

std::unique_ptr<solver::State> Nav2DModel::sampleAnInitState() {
    RandomGenerator &randGen = *getRandomGenerator();
    double areaValue = std::uniform_real_distribution<double>(0,
            totalStartArea_)(randGen);
    double areaTotal = 0;
    for (AreasById::value_type const &entry : startAreas_) {
        areaTotal += entry.second.getArea();
        if (areaValue < areaTotal) {
            return std::make_unique<Nav2DState>(
                    entry.second.sampleUniform(randGen), 0,
                    costPerUnitDistance_, costPerRevolution_);
        }
    }
    cerr << "ERROR: Invalid area at " << areaValue << endl;
    return nullptr;
}

std::unique_ptr<solver::State> Nav2DModel::sampleStateUniform() {
    return sampleStateAt(mapArea_.sampleUniform(*getRandomGenerator()));
}

bool Nav2DModel::isTerminal(solver::State const &state) {
    return isInside(static_cast<Nav2DState const &>(state).getPosition(),
            AreaType::GOAL);
}

double Nav2DModel::getHeuristicValue(solver::State const &state) {
    Nav2DState const &navState = static_cast<Nav2DState const &>(state);
    double distance = getDistance(navState.getPosition(), AreaType::GOAL);
    double value = goalReward_;
    value -= costPerUnitDistance_ * distance;
    value -= costPerUnitTime_ * distance / maxSpeed_;
    return value;
}

double Nav2DModel::getDefaultVal() {
    return minVal_;
}

std::pair<std::unique_ptr<Nav2DState>, double> Nav2DModel::tryPath(
           Nav2DState const &state, double speed, double rotationalSpeed) {
    Point2D position = state.getPosition();
    double direction = state.getDirection();
    double radius = speed / (2 * M_PI * rotationalSpeed);
    double turnAmount = rotationalSpeed * timeStepLength_;
    Vector2D velocity(speed, direction);

    std::unique_ptr<Nav2DState> resultingState(nullptr);
    bool inGoal = false;
    bool hasCollision = false;

    double currentScalar = 0;
    Point2D currentPosition = position;
    double currentDirection = direction;
    Point2D center = position + Vector2D(radius, direction +
            turnAmount > 0 ? 0.25 : -0.25);

    for (long step = 1; step <= interpolationStepCount_; step++) {
        Point2D previousPosition = currentPosition;
        double previousDirection = currentDirection;
        double previousScalar = currentScalar;

        currentScalar = (double)step / interpolationStepCount_;
        if (turnAmount == 0) {
            currentPosition = position + currentScalar * velocity;
        } else {
            currentDirection = direction + currentScalar * turnAmount;
            currentPosition = center + Vector2D(radius, currentDirection +
                               turnAmount > 0 ? 0.25 : -0.25);
        }
        if (!mapArea_.contains(currentPosition) ||
                isInside(currentPosition, AreaType::OBSTACLE)) {
            currentScalar = previousScalar;
            currentPosition = previousPosition;
            currentDirection = previousDirection;
            hasCollision = true;
            break;
        }
        if (isInside(currentPosition, AreaType::GOAL)){
            inGoal = true;
            break;
        }
    }
    resultingState = std::make_unique<Nav2DState>(
            currentPosition.getX(),
            currentPosition.getY(),
            currentDirection,
            costPerUnitDistance_,
            costPerRevolution_);
    double actualDistance;
    double actualTurnAmount;
    if (turnAmount == 0) {
        actualDistance = (currentPosition - position).getMagnitude();
        actualTurnAmount = 0;
    } else {
        actualTurnAmount = std::abs(currentScalar * turnAmount);
        actualDistance = 2 * M_PI * actualTurnAmount * radius;
    }
    double reward = 0;
    reward -= costPerUnitTime_ * timeStepLength_;
    reward -= costPerUnitDistance_ * actualDistance;
    reward -= costPerRevolution_ * actualTurnAmount;
    if (inGoal) {
        reward += goalReward_;
    }
    if (hasCollision) {
        reward -= crashPenalty_;
    }
    return std::make_pair(std::move(resultingState), reward);
}

std::unique_ptr<solver::State> Nav2DModel::generateNextState(
           solver::State const &state, solver::Action const &action) {
    Nav2DAction const &navAction = static_cast<Nav2DAction const &>(action);
    double speed = applySpeedError(navAction.getSpeed());
    double rotationalSpeed = applyRotationalError(
            navAction.getRotationalSpeed());
    return tryPath(static_cast<Nav2DState const &>(state), speed,
            rotationalSpeed).first;
}

std::unique_ptr<solver::Observation> Nav2DModel::generateObservation(
        solver::Action const &/*action*/, solver::State const &nextState) {
    Nav2DState const &navState = static_cast<Nav2DState const &>(nextState);
    if (isInside(navState.getPosition(), AreaType::OBSERVATION)) {
        return std::make_unique<Nav2DObservation>(navState);
    } else {
        return std::make_unique<Nav2DObservation>();
    }
}

double Nav2DModel::getReward(solver::State const &/*state*/,
        solver::Action const &/*action*/,
        solver::State const */*nextState*/) {
    cerr << "ERROR: Cannot calculate reward!" << endl;
    return 0;
}

solver::Model::StepResult Nav2DModel::generateStep(
        solver::State const &state,
        solver::Action const &action) {
    Nav2DState const &navState = static_cast<Nav2DState const &>(state);
    Nav2DAction const &navAction = static_cast<Nav2DAction const &>(action);
    double speed = applySpeedError(navAction.getSpeed());
    double rotationalSpeed = applyRotationalError(
            navAction.getRotationalSpeed());

    std::unique_ptr<Nav2DState> nextState;
    double reward;
    std::tie(nextState, reward) = tryPath(navState, speed, rotationalSpeed);
    solver::Model::StepResult result;
    result.action = action.copy();
    result.nextState = std::move(nextState);
    result.isTerminal = isTerminal(*result.nextState);
    result.observation = generateObservation(action, *result.nextState);
    result.reward = reward;
    return result;
}

std::vector<long> Nav2DModel::loadChanges(char const *changeFilename) {
    std::vector<long> changeTimes;
       std::ifstream ifs;
       ifs.open(changeFilename);
       std::string line;
       while (std::getline(ifs, line)) {
           std::istringstream sstr(line);
           std::string tmpStr;
           long time;
           long nChanges;
           sstr >> tmpStr >> time >> tmpStr >> nChanges;

           changes_[time] = std::vector<Nav2DChange>();
           changeTimes.push_back(time);
           for (int i = 0; i < nChanges; i++) {
               std::getline(ifs, line);
               sstr.clear();
               sstr.str(line);

               Nav2DChange change;
               sstr >> change.operation;
               if (change.operation != "ADD") {
                   cerr << "ERROR: Cannot " << change.operation;
                   continue;
               }
               std::string typeString;
               sstr >> typeString;
               change.type = parseAreaType(typeString);
               sstr >> change.id;
               sstr >> change.area;
               changes_[time].push_back(change);
           }
       }
       ifs.close();
       return changeTimes;
}

void Nav2DModel::update(long time, solver::StatePool *pool) {
    for (Nav2DChange &change : changes_[time]) {
        cout << areaTypeToString(change.type) << " " << change.id;
        cout << " " << change.area << endl;
        addArea(change.id, change.area, change.type);
        solver::FlaggingVisitor visitor(pool, solver::ChangeFlags::DELETED);
        solver::RTree *tree = static_cast<solver::RTree *>(
                pool->getStateIndex());
        if (change.type == AreaType::OBSERVATION) {
            visitor.flagsToSet_ = solver::ChangeFlags::OBSERVATION_BEFORE;
        }
        tree->boxQuery(visitor,
                { change.area.getLowerLeft().getX(),
                        change.area.getLowerLeft().getY(), -2.0 },
                { change.area.getUpperRight().getX(),
                        change.area.getUpperRight().getY(), -2.0 });
    }
}

geometry::RTree *Nav2DModel::getTree(AreaType type) {
    switch(type) {
    case AreaType::GOAL:
        return &goalAreaTree_;
    case AreaType::OBSTACLE:
        return &obstacleTree_;
    case AreaType::START:
        return &startAreaTree_;
    case AreaType::OBSERVATION:
        return &observationAreaTree_;
    default:
        cerr << "ERROR: Cannot get tree; type " << static_cast<long>(type);
        cerr << endl;
        return nullptr;
    }
}

Nav2DModel::AreasById *Nav2DModel::getAreas(AreaType type) {
    switch(type) {
    case AreaType::GOAL:
        return &goalAreas_;
    case AreaType::OBSTACLE:
        return &obstacles_;
    case AreaType::START:
        return &startAreas_;
    case AreaType::OBSERVATION:
        return &observationAreas_;
    default:
        cerr << "ERROR: Cannot get area; type " << static_cast<long>(type);
        cerr << endl;
        return nullptr;
    }
}

bool Nav2DModel::isInside(geometry::Point2D point, AreaType type) {
    for (AreasById::value_type &entry : *getAreas(type)) {
        if (entry.second.contains(point)) {
            return true;
        }
    }
    return false;
    /*
    geometry::RTree *tree = getTree(type);
    SpatialIndex::Point p(&(point.asVector()[0]), nStVars_);
    class MyVisitor: public SpatialIndex::IVisitor {
    public:
        bool isInside = false;
        void visitNode(SpatialIndex::INode const &) {}
        void visitData(std::vector<SpatialIndex::IData const *> &) {}
        void visitData(SpatialIndex::IData const &) {
            isInside = true;
        }
    };
    MyVisitor v;
    tree->getTree()->pointLocationQuery(p, v);
    return v.isInside;
    */
}

double Nav2DModel::getDistance(geometry::Point2D point, AreaType type) {
    double distance = std::numeric_limits<double>::infinity();
    for (AreasById::value_type &entry : *getAreas(type)) {
        double newDistance = entry.second.distanceTo(point);
        if (newDistance < distance) {
            distance = newDistance;
        }
    }
    return distance;
    /*
    geometry::RTree *tree = getTree(type);
    SpatialIndex::Point p(&(point.asVector()[0]), nStVars_);
    class MyVisitor: public SpatialIndex::IVisitor {
    public:
        SpatialIndex::Point &p_;
        double distance_;
        MyVisitor(SpatialIndex::Point &p) :
                p_(p), distance_(std::numeric_limits<double>::infinity()) {}
        void visitNode(SpatialIndex::INode const &) {}
        void visitData(std::vector<SpatialIndex::IData const *> &) {
        }
        void visitData(SpatialIndex::IData const &data) {
            SpatialIndex::IShape *shape;
            data.getShape(&shape);
            distance_ = shape->getMinimumDistance(p_);
        }
    };
    MyVisitor v(p);
    tree->getTree()->nearestNeighborQuery(1, p, v);
    return v.distance_;
    */
}

Nav2DModel::AreaType Nav2DModel::getAreaType(geometry::Point2D point) {
    if (!mapArea_.contains(point)) {
        return AreaType::OUT_OF_BOUNDS;
    } else if (isInside(point, AreaType::OBSTACLE)) {
        return AreaType::OBSTACLE;
    } else if (isInside(point, AreaType::GOAL)) {
        return AreaType::GOAL;
    } else if (isInside(point, AreaType::START)) {
        return AreaType::START;
    } else if (isInside(point, AreaType::OBSERVATION)) {
        return AreaType::OBSERVATION;
    } else {
        return AreaType::EMPTY;
    }
}

void Nav2DModel::dispPoint(Nav2DModel::AreaType type, std::ostream &os) {
    switch(type) {
    case AreaType::EMPTY:
        os << " ";
        return;
    case AreaType::START:
        os << "+";
        return;
    case AreaType::GOAL:
        os << "*";
        return;
    case AreaType::OBSTACLE:
        os << "%";
        return;
    case AreaType::OBSERVATION:
        os << "x";
        return;
    case AreaType::OUT_OF_BOUNDS:
        os << "#";
        return;
    default:
        cerr << "ERROR: Invalid point type!?" << endl;
        return;
    }
}

void Nav2DModel::drawEnv(std::ostream &os) {
    double minX = mapArea_.getLowerLeft().getX();
    double maxX = mapArea_.getUpperRight().getX();
    double minY = mapArea_.getLowerLeft().getY();
    double maxY = mapArea_.getUpperRight().getY();
    double height = maxY - minY;
    long nRows = 30; //(int)height;
    double width = maxX - minX;
    long nCols = (int)width;
    for (long i = 0; i <= nRows + 1; i++) {
        double y = (nRows + 0.5 - i) * height / nRows;
        for (long j = 0; j <= nCols + 1; j++) {
            double x = (j - 0.5) * width / nCols;
            dispPoint(getAreaType({x, y}), os);
        }
        os << endl;
    }
}

void Nav2DModel::drawState(solver::State const &state, std::ostream &os) {
    Nav2DState const &navState = static_cast<Nav2DState const &>(state);
    double minX = mapArea_.getLowerLeft().getX();
    double maxX = mapArea_.getUpperRight().getX();
    double minY = mapArea_.getLowerLeft().getY();
    double maxY = mapArea_.getUpperRight().getY();
    double height = maxY - minY;
    long nRows = 30; //(int)height;
    double width = maxX - minX;
    long nCols = (int)width;

    long stateI = nRows - (int)std::round(navState.getY() * nRows / height - 0.5);
    long stateJ = (int)std::round(navState.getX() * nCols / width + 0.5);
    for (long i = 0; i <= nRows + 1; i++) {
        double y = (nRows + 0.5 - i) * height / nRows;
        for (long j = 0; j <= nCols + 1; j++) {
            double x = (j - 0.5) * width / nCols;
            if (i == stateI && j == stateJ) {
                os << "o";
            } else {
                dispPoint(getAreaType({x, y}), os);
            }
        }
        os << endl;
    }
    os << state << endl;
}

enum class ActionType {
    FORWARD_0 = 0,
    FORWARD_1 = 1,
    FORWARD_2 = 2,
    TURN_LEFT_0 = 3,
    TURN_LEFT_1 = 4,
    TURN_LEFT_2 = 5,
    TURN_RIGHT_0 = 6,
    TURN_RIGHT_1 = 7,
    TURN_RIGHT_2 = 8,
    DO_NOTHING = 9,
};

long Nav2DModel::getNumberOfBins() {
    return 10;
}

std::unique_ptr<solver::EnumeratedPoint> Nav2DModel::sampleAnAction(
        long code) {
    switch(static_cast<ActionType>(code)) {
    case ActionType::FORWARD_0:
        return std::make_unique<Nav2DAction>(code, 0.2 * maxSpeed_, 0.0);
    case ActionType::FORWARD_1:
        return std::make_unique<Nav2DAction>(code, 0.6 * maxSpeed_, 0.0);
    case ActionType::FORWARD_2:
        return std::make_unique<Nav2DAction>(code, maxSpeed_, 0.0);
    case ActionType::TURN_LEFT_0:
        return std::make_unique<Nav2DAction>(
                code, 0.0,0.2 * maxRotationalSpeed_);
    case ActionType::TURN_LEFT_1:
        return std::make_unique<Nav2DAction>(
                code, 0.0, 0.6 * maxRotationalSpeed_);
    case ActionType::TURN_LEFT_2:
        return std::make_unique<Nav2DAction>(code, 0.0, maxRotationalSpeed_);
    case ActionType::TURN_RIGHT_0:
        return std::make_unique<Nav2DAction>(
                code, 0.0, -0.2 * maxRotationalSpeed_);
    case ActionType::TURN_RIGHT_1:
        return std::make_unique<Nav2DAction>(
                code, 0.0, -0.6 * maxRotationalSpeed_);
    case ActionType::TURN_RIGHT_2:
        return std::make_unique<Nav2DAction>(code, 0.0, -maxRotationalSpeed_);
    case ActionType::DO_NOTHING:
        return std::make_unique<Nav2DAction>(code, 0.0, 0.0);
    default:
        cerr << "ERROR: Invalid Action Code " << code;
        return nullptr;
    }
}

double Nav2DModel::getMaxObservationDistance() {
    return maxObservationDistance_;
}
} /* namespace nav2d */