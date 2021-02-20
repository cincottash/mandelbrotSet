import cv2
import numpy as np
import math
import time
import pickle

def serializeMandelbrotSet(n):

	totalPixels = n*n
	#nxn with space for RGB
	img = np.zeros(shape=[n, n, 3], dtype=np.uint64)
	
	#Make n equally spaced values between -2 and 2
	xValuesList = list(np.linspace(-2, 2, n))
	yValuesList = list(np.linspace(-2, 2, n))

	u=0
	pixelsDone = 0
	start = time.time()
	for x in range(n):
		v=0
		for y in range(n):
			
			if(round(time.time() - start, 2) % 1 == 0):
				percentDone = pixelsDone/n**2 * 100
				print("Percent done: {}".format(round(percentDone, 2)))
			
			z = 0 + 0j
			c = complex(xValuesList[x], yValuesList[y])
			for i in range(100):
				z = z*z + c
				if abs(z) > 2.0:
					#normalizes z between 0 and 255
					normalZ = int(-255*((abs(z)/4)-1.5))
				
					cosGradiant = abs(math.cos(x * y))

					rValue = normalZ * cosGradiant * 0.85
					gValue = normalZ * cosGradiant * 0.75
					bValue = normalZ * cosGradiant

					img[u][v] = (bValue, gValue, rValue)
					break
				#the middle portion
				rValue = int(x*y/totalPixels * 255)
				gValue = int(x*y/totalPixels * 255/8)
				bValue = int(x*y/totalPixels * 255)

				img[u][v] = ((bValue, gValue, rValue))
			
			pixelsDone += 1

			v+=1
		u+=1
	
	#save numpy img before we write incase of crash while writing, so we can load the img and try again later
	with open('{}cos.pickle'.format(str(n)), 'wb') as handle:
		pickle.dump(img, handle, protocol=pickle.HIGHEST_PROTOCOL)
