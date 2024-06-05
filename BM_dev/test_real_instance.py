import matplotlib.pyplot as plt
from tabulate import tabulate
from vrplib import read_solution

from pyvrp import Model, read
from pyvrp.plotting import (
    plot_coordinates,
    plot_instance,
    plot_result,
    plot_route_schedule,
)
from pyvrp.stop import MaxIterations, MaxRuntime

INSTANCE = read("BM_dev/BM_instances/X-n186-k15_C13_unit-demand.vrp", round_func="round")
BKS = read_solution("BM_dev/BM_instances/X-n186-k15_C13_unit-demand.sol")

model = Model.from_data(INSTANCE)
result = model.solve(stop=MaxIterations(5000), seed=42, display=True)
print(result)