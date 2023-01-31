#include "Individual.h"
#include "bindings.h"

namespace py = pybind11;

void bind_Individual(py::module_ &m)
{
    py::class_<Individual>(m, "Individual")
        .def(py::init<ProblemData &, PenaltyManager &, XorShift128 &>(),
             py::arg("data"),
             py::arg("penalty_manager"),
             py::arg("rng"))
        .def(py::init<ProblemData &,
                      PenaltyManager &,
                      std::vector<std::vector<int>>>(),
             py::arg("data"),
             py::arg("penalty_manager"),
             py::arg("routes"))
        .def("cost", &Individual::cost)
        .def("num_routes", &Individual::numRoutes)
        .def("get_routes", &Individual::getRoutes)
        .def("get_neighbours", &Individual::getNeighbours)
        .def("is_feasible", &Individual::isFeasible)
        .def("has_excess_capacity", &Individual::hasExcessCapacity)
        .def("has_time_warp", &Individual::hasTimeWarp)
        .def("to_file", &Individual::toFile);
}