import numpy as np
import pickle
import sys

maxIterations = 100

#n shud be 2/3m

def serializeMandelbrotSet(m, n):

	x = np.linspace(-2, 1, num=m).reshape((1, m))
	y = np.linspace(-1, 1, num=n).reshape((n, 1))
	C = np.tile(x, (n, 1)) + 1j * np.tile(y, (1, m))
	 
	Z = np.zeros((n, m), dtype=complex)
	M = np.full((n, m), True, dtype=bool)
	for i in range(maxIterations):
		Z[M] = Z[M] * Z[M] + C[M]
		M[np.abs(Z) > 2] = False

	with open('{}x{}man.pickle'.format(str(m), str(n)), 'wb') as handle:
		pickle.dump(M, handle, protocol=pickle.HIGHEST_PROTOCOL)
 



def main():
	m = int(sys.argv[1])
	n = int(sys.argv[2])
	serializeMandelbrotSet(m, n)

main()