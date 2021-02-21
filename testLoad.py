import pickle
import numpy as np
from imageio import imwrite
import sys

def main():
	fileName = sys.argv[1]

	print("Reading image from {}".format(fileName))

	with open(fileName + '.pickle', 'rb') as handle:
		M = pickle.load(handle)

	print("Writing image to file")
	imwrite(fileName + '.png', np.uint8(np.flipud(1 - M) * 255))

main()
