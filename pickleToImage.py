import pickle
import cv2
import sys

def main():
	fileName = sys.argv[1]
	with open(fileName + '.pickle', 'rb') as handle:
		img = pickle.load(handle)

	cv2.imwrite(fileName + '.png', img) 

main()