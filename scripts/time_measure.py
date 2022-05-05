import re
import sys

text = sys.stdin.read()
pattern=re.compile('Took (.+) microseconds') 
result=pattern.findall(text)
print("Took " + result[0] + " microseconds")
