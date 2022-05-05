import re
import sys

text = sys.stdin.read()
pattern=re.compile('Took (.+) microseconds') 
result=pattern.findall(text)
print("Took " + result[0] + " microseconds")

# $ sudo docker logs c4e527f25017d9b47630cb9b072a5349ae9ee8797f5f99511c690ac4c50de65a | python time_measure.py 
# Took 96265 microseconds