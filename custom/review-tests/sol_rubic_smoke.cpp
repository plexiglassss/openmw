#include <sol/sol.hpp>
#include <iostream>
#include <memory>
#include <stdexcept>
struct Payload {
    static inline int alive = 0;
    sol::object retained;
    explicit Payload(sol::object o):retained(std::move(o)){++alive;}
    Payload(const Payload& p):retained(p.retained){++alive;}
    ~Payload(){--alive;}
};
int main(){
    sol::state lua;
    lua.open_libraries(sol::lib::base,sol::lib::table,sol::lib::string,sol::lib::jit);
    lua.new_usertype<Payload>("Payload",sol::no_constructor);
    lua.set_function("makePayload",[](sol::object obj){return std::make_shared<Payload>(std::move(obj));});
    auto r=lua.safe_script(R"(
        local weak = setmetatable({}, {__mode='v'})
        for pass=1,20 do
            local objects={}
            for i=1,1000 do
                objects[i]=makePayload({id=i,label='object-'..i})
                weak[i]=objects[i]
            end
            assert(objects[1000]~=nil)
            objects=nil
            collectgarbage('collect')
            collectgarbage('collect')
            assert(next(weak)==nil)
        end
    )",sol::script_pass_on_error);
    if(!r.valid()){sol::error e=r;std::cerr<<e.what();return 1;}
    lua.collect_garbage();lua.collect_garbage();
    if(Payload::alive!=0){std::cerr<<"Leaked bound payloads: "<<Payload::alive;return 2;}
    std::cout<<"PASS: OpenMW's pinned sol3: shared_ptr userdata + retained Lua objects released after GC\n";
}
