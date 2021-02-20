import time
import random
from drawMandelbrotSet import *
import sys
def main():
	n = int(sys.argv[1])

	serializeMandelbrotSet(n)
if __name__ == '__main__':
	main()

