import sys
import numpy as np

assert(len(sys.argv) == 2)
memory_file = sys.argv[1]
mems = []
with open(memory_file,"r") as f:
    for line in f:
        line = line.split()
        mems.append(float(line[-1]))

print("Average memory: " + str(np.mean(mems)))