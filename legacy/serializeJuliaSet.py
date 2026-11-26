import cv2
import numpy as np
import math
import time
import pickle
import sys

maxIterations = 50

def serializeJuliaSet(n):

	totalPixels = n*n
	#nxn with space for RGB
	img = np.zeros(shape=[n, n, 3], dtype=np.uint64)
	
	#Make n equally spaced values between -2 and 2
	xValuesList = list(np.linspace(-1, 1, n))
	yValuesList = list(np.linspace(-1, 1, n))

	u=0
	pixelsDone = 0
	start = time.time()
	#c = −0.8 + 0.156i
	c = complex(-0.8, 0.156)
	for x in range(n):
		v=0
		for y in range(n):
			if(round(time.time() - start, 2) % 1 == 0):
				percentDone = pixelsDone/n**2 * 100
				print("Percent done: {}".format(round(percentDone, 2)))
			
			z = complex(xValuesList[x],yValuesList[y])
			
			for i in range(maxIterations):
				z = z*z + c
				
				if abs(z) > 10.0:
					cosGradiant = abs(math.cos(x * y))
					#zs seems to not go above 100
					normalZ = abs(z)/100 * 255
					#print(normalZ)
					rValue = normalZ * .1529
					gValue = normalZ * .0823
					bValue = normalZ * .5784
					
					img[u][v] = (bValue, gValue, rValue)
					break
				#the middle portion
				rValue = int((x*y)/totalPixels * 255)
				gValue = int((x*y)/totalPixels * 255/8)
				bValue = int((x*y)/totalPixels * 255)

				img[u][v] = ((bValue, gValue, rValue))
			
			pixelsDone += 1

			v+=1
		u+=1
	
	#save numpy img before we write incase of crash while writing, so we can load the img and try again later
	with open('{}jul.pickle'.format(str(n)), 'wb') as handle:
		pickle.dump(img, handle, protocol=pickle.HIGHEST_PROTOCOL)


def main():
	n = int(sys.argv[1])

	serializeJuliaSet(n)

main()