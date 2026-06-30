int main()
{
	int z=3;
	auto f=[=](auto &self,int n,int (*p)(int))->int
	{
		if(!n)
			return p(z);
		else
			return self(self,n-1,[=](int x) -> int {return p(x + n + z);});
	};
	int n=5;
	return f(f,n,[](int x)->int{ return x; });
}