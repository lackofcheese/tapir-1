#include "problems/shared/solve.hpp"

#include "HomecareModel.hpp"                 // for HomecareModel
#include "HomecareOptions.hpp"               // for HomecareOptions

int main(int argc, char const *argv[]) {
    return solve<homecare::HomecareModel, homecare::HomecareOptions>(argc, argv);
}
