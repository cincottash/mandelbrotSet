import pickle
import cv2
import sys

def main():
	fileName = str(sys.argv[1])

	if not fileName.endswith('.pickle'):
		fileName +='.pickle'

	with open(fileName, 'rb') as handle:
		img = pickle.load(handle)

	print("Writing image to file")
	cv2.imwrite(fileName.replace('.pickle', '') + '.png', img) 

main()