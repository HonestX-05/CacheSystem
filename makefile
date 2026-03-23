test : testAllCachePolicy.cpp
	g++ testAllCachePolicy.cpp -o test
.PHONY : clean rebulid
clean : 
	${RM} test
rebulid : clean test

