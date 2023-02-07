#include "diversity.h"

#include <pybind11/pybind11.h>

namespace py = pybind11;

PYBIND11_MODULE(broken_pairs_distance, m)
{
    m.def("broken_pairs_distance",
          &brokenPairsDistance,
          py::arg("data"),
          py::arg("first"),
          py::arg("second"));
}