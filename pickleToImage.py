import pickle
import cv2
import sys

def main():
	fileName = sys.argv[1]
	with open(fileName + '.pickle', 'rb') as handle:
		img = pickle.load(handle)

	print("Writing image to file")
	cv2.imwrite(fileName + '.png', img) 

main()