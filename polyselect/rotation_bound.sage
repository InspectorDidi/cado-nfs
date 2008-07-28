
# Here follows some code for estimating an intelligent rotation bound for
# a given polynomial pair, by making a compromise between:
# - possibly increasing the norm
# - getting better alpha values

# use as follows:
# attach rotation_bound.sage
# define f and g
# Try the ``Try this'' examples below.

import os,time

attach alpha.sage
attach integral.sage


# optional
attach gnuplot_stuff.sage

# Experimentally, for degree 5, the *affine* contribution to alpha has
# approximately this shape. The projective contribution is disregarded
# because rotation leaves it unaffected.
# Note that the table has indices scaled by 10.
reduced_alpha_affine_table=build_reduced_minimum_normal_table(0.571,0.851,0.05)

##### Norm computations.

def deskew_polynomial(f,s):
    """Utility function. Returns a polynomial over the reals."""
    d=f.degree()
    g=[]
    ss=1
    for i in range(d):
        g.append(float(f[i])*ss)
        ss*=float(s)
    g.append(f[d]*ss)
    return PolynomialRing(RealField(),'x')(g), ss

def supnorm(f,s):
    g,ss=deskew_polynomial(f,s)
    return max([abs(c) for c in g.coefficients()])/sqrt(ss)

def l1norm(f,s):
    g,ss=deskew_polynomial(f,s)
    return sum([abs(c) for c in g.coefficients()])/sqrt(ss)

def l2norm(f,s):
    g,ss=deskew_polynomial(f,s)
    return sqrt(sum([c^2 for c in g.coefficients()])/ss)

def square_evenpart(f):
    """Utility function. Computes the even part of the square of the
    polynomial f """
    d=f.degree()
    g=f.parent()(([sum([f[2*i-j]*f[j] for j in [0..2*i]]) for i in [0..d]]))
    return g

def l2norm_tk(f,s):
    """
    This norm gives the half-square-root of the integral of f^2 over the
    square [-1,1]^2, taking into account the given skewness.
    Taking half the square root is not really correct, but that's what TK
    does (out of a mistake, apparently).
    """
    g,ss=deskew_polynomial(f,s)
    g2=square_evenpart(g)
    d=f.degree()
    coeffs=[4/(2*i+1)/(2*(d-i)+1) for i in [0..d]]
    return 1/2*sqrt(vector(g2.coefficients())*vector(coeffs)/ss)



##### Optimizing norms.

# For the sup norm, it's rather special: there's a fairly easy way to
# compute the best skewness from the graph log(norm) as a function of
# log(s)

def supnorm_hull_inner(f,logs):
    d=f.degree()
    i=0
    while f[i] == 0:
        i+=1
    l=[(-Infinity,i)]
    i+=1
    while i<=d:
        while True:
            t=l.pop()
            sx=float((logs[i]-logs[t[1]])/(t[1]-i))
            if sx >= t[0]:
                # This one was good, put it back in
                l.append(t)
                break
        l.append((sx,i))
        i+=1
    return l

def supnorm_hull(f):
    """
    Return the sup of the lines giving log(supnorm) as a function of
    log(s)
    """
    d=f.degree()
    logs=[float(log(abs(a))) if a != 0 else -Infinity for a in f.coeffs()]
    l=supnorm_hull_inner(f,logs)
    if l[0][0] == -Infinity:
        l.pop(0)
    # returns index, logskew, skew, lognorm
    return [ (v[1],v[0],exp(v[0]),logs[v[1]]+(v[1]-d/2)*v[0]) for v in l ]

def skew_supnorm(f):
    h=[(v[3],v[2]) for v in supnorm_hull(f)]
    m=min(h)
    return m[1]

def unique_positive_real_root(f):
    """Utility function. Assuming f has only one real root > 0, return
    it. Fail if the assumption does not hold."""
    r=f.real_roots()
    sqrt_root_pos=[s for s in r if s > 0]
    assert(len(sqrt_root_pos)==1)
    return sqrt_root_pos[0]

def skew_l1norm(f):
    d=f.degree()
    ZP=f.parent()
    # This is not the derivative, but rather 2s^(d/2+1) times the
    # derivative.
    g=ZP([abs(f[i])*(2*i-d) for i in [0..d]])
    return unique_positive_real_root(g)

def skew_l2norm(f):
    d=f.degree()
    ZP=f.parent()
    # This is not the derivative. It's rather, once evaluated at s^2,
    # s^(d+1) times the derivative of the square.
    g=ZP([f[i]^2*(2*i-d) for i in [0..d]])
    return sqrt(unique_positive_real_root(g))

def skew_l2norm_tk(f):
    """Gives the optimal skew for the L2 norm of a polynomial f"""
    d=f.degree()
    ZP=f.parent()
    x=ZP.gen()
    coeffs=[4*(2*i-d)*x^i/(2*i+1)/(2*(d-i)+1) for i in [0..d]]
    dd=vector(square_evenpart(f).coefficients())*vector(coeffs)
    return sqrt(unique_positive_real_root(dd))

# Return the best chosen norm
def best_supnorm(f):
    return supnorm(f, skew_supnorm(f))

def best_l1norm(f):
    return l1norm(f, skew_l1norm(f))

def best_l2norm(f):
    return l2norm(f, skew_l2norm(f))

def best_l2norm_tk(f):
    return l2norm_tk(f, skew_l2norm_tk(f))


### We can thus plot the norm with respect to the chosen skew:

def norms_from_s_for_plotting(f,g):
    def mine(c,t):
        return ((lambda s: flog(c(f, fexp10(s)))),t)
    l=[]
    l.append(mine(l1norm,"L1"))
    l.append(mine(l2norm,"L2"))
    l.append(mine(supnorm,"sup"))
    l.append(mine(l2norm_tk,"L2-int"))
    return l
# Try this:
#       myplot(norms_from_s_for_plotting(f,g), 3,6, 100)

# This shows (using for example the RSA768 polynomial) that the L2
# integral norm takes well into account the presence of real roots.



### We now turn to the sensibility of the norm(s) with respect to a0.

# utility stuff
def flog(x): return float(log(abs(float(x))))
def fexp(x): return float(exp(float(x)))
def flog10(x): return float(log(abs(float(x)))/log(10))
def fexp10(x): return float(exp(float(x*log(10))))

def change_ccoeff(f,a0):
    return PolynomialRing(RealField(),'x')(f-f[0])+a0



# For the supnorm, the algorithm is slightly special
def supnorm_hull_extra(f):
    """
    Return the hull of the lines giving log(supnorm) as a function of
    log(s). Also gives extra information utilized in order to give the
    graph log(best supnorm) as a function of log(a0) and log(a1)
    """
    d=f.degree()
    c=1
    logs=[float(log(abs(a))) if a != 0 else -Infinity for a in f.coeffs()]
    f1=f-f[0]
    l=[(v[0],v[1],logs[v[1]]+(v[1]-d/2)*v[0])           \
                for v in supnorm_hull_inner(f1,logs)]
    while len(l) > 1 and l[0][2] > l[1][2]:
        l.pop(0)
    cuts=[]
    for v in l:
        # Which value of a0 matches this ?
        # We have to solve
        # log(a0)-d/2*log(s) == v[2]
        log_a0_cutoff = v[2]+d/2*v[0]
        log_a0_slope = (v[1]-d/2)/v[1]
        cuts.append((log_a0_cutoff,log_a0_slope,v[0],v[2]))
    return cuts;

def log_best_supnorm_from_log_a_quick(h,la):
    if la < h[0][0]:
        r=h[0][3]
    else:
        k=-1
        while la < h[k][0]:
            k-=1
        r=h[k][3]+h[k][1]*(la-h[k][0])
    return float(r)*1.0

def log_best_supnorm_from_log_a(f,la):
    h=supnorm_hull_extra(f)
    return log_best_supnorm_from_log_a_quick(h,la)

# Apart from the supnorm, other norms are accounted for in a standard
# manner.

def norms_from_a0_for_plotting(f,g):
    def mine(c,t,m):
        return ((lambda k: flog(c(change_ccoeff(f, m*fexp10(k))))),t)
    l=[]
    l.append(mine(best_l1norm,"L1",1))
    l.append(mine(best_l2norm,"L2",1))
    l.append(mine(best_supnorm,"sup",1))
    l.append(mine(best_l2norm_tk,"L2-int",1))
    l.append(mine(best_l2norm_tk,"L2-int",-1))
    return l
# Try this:
#       myplot(norms_from_a0_for_plotting(f,g), 38, 47, 100)
# Notice how the behaviour is a lot more intricate for the L2 integral norm.
# Our two plots give respecively f+k*g and f-k*g, which turn out to be
# somewhat different.

# From now on, we disregard norms other that the L2 integral norm.

def norms_from_a0_for_plotting_sconstant(f,g):
    def smine(c,s,t,m):
        return ((lambda k: flog(c(change_ccoeff(f, m*fexp10(k)), \
                s))),t)
    l=[]
    #l.append(smine(l1norm,skew_l1norm(f),"L1 (constant s)",1))
    #l.append(smine(l2norm,skew_l2norm(f),"L2 (constant s)",1))
    #l.append(smine(supnorm,skew_supnorm(f),"sup (constant s)",1))
    l.append(smine(l2norm_tk,skew_l2norm_tk(f),"L2-int (constant s)",1))
    l.append(smine(l2norm_tk,skew_l2norm_tk(f),"L2-int (constant s)",-1))
    return l

def sconstant_or_optimal(f,g):
    def mine(c,t,m):
        return ((lambda k: flog(c(change_ccoeff(f, m*fexp10(k))))),t)
    def smine(c,s,t,m):
        return ((lambda k: flog(c(change_ccoeff(f, m*fexp10(k)), \
                s))),t)
    l=[]
    l.append(mine(best_l2norm_tk,"L2-int",1))
    l.append(mine(best_l2norm_tk,"L2-int",-1))
    l.append(smine(l2norm_tk,skew_l2norm_tk(f),"L2-int (constant s)",1))
    l.append(smine(l2norm_tk,skew_l2norm_tk(f),"L2-int (constant s)",-1))
    return l
# Try this:
#       myplot(sconstant_or_optimal(f,g), 40, 45, 100)
# This indicates how a sensible choice for the skewness impacts the norm
# in the end.


# Now we focus on the rotation f+k*g. Given a bound 10^w on the size of
# the rotation area, try to have a guess on what is attainable in terms
# of log(norm)+bbest alpha.


# again, special case for the sup norm
def logsupnorm_plus_alpha_rot(f,g,w,h):
    """
    Gives the expected log(sup norm) + alpha value with rotation bounded
    by w. The result takes into account the alpha value that we have at
    least 84% chances finding (at worst 1 standard deviation above the
    mean).
    """
    lf0=flog(f[0])
    lm=flog(g[0])
    lognorm=log_best_supnorm_from_log_a_quick(h,max(lf0,w*flog(10)+lm))
    alpha=eval_dichotomy(reduced_alpha_affine_table,10.0*w)
    return float(lognorm+alpha[0]+alpha[1])

def lognorm_plus_alpha_rot(f,g,normfunc,w):
    """
    Gives the expected log(chosen norm) + alpha value with rotation bounded
    by 10^w. The result takes into account the alpha value that we have at
    least 84% chances finding (at worst 1 standard deviation above the
    mean).
    """
    RP=PolynomialRing(RealField(),'z');
    E=[fexp10(w), -fexp10(w)]
    lognorm=min([flog(normfunc(RP(f)+e*RP(g))) for e in E])
    alpha=eval_dichotomy(reduced_alpha_affine_table,10.0*(w))
    return float(lognorm+alpha[0]+alpha[1])

def lognorm_plus_alpha_rot_scons(f,g,normfunc,skew,w):
    RP=PolynomialRing(RealField(),'z');
    E=[fexp10(w), -fexp10(w)]
    lognorm=min([flog(normfunc(RP(f)+e*RP(g), skew)) for e in E])
    alpha=eval_dichotomy(reduced_alpha_affine_table,10.0*(w))
    return float(lognorm+alpha[0]+alpha[1])

def lognorm_plus_alpha_rot_scons_linear(f,g,normfunc,skew,w):
    s=flog(skew)
    logb=(w*flog(10)+s)/2
    loga=(w*flog(10)-s)/2
    RP=PolynomialRing(RealField(),'z');
    z=RP.gen()
    Ea=fexp(loga); Eb=fexp(logb)
    E=[ Ea*z+Eb, Ea*z-Eb, -Ea*z+Eb, -Ea*z-Eb ];
    lognorm=min([flog(normfunc(RP(f)+e*RP(g), skew)) for e in E])
    alpha=eval_dichotomy(reduced_alpha_affine_table,10.0*(w))
    return float(lognorm+alpha[0]+alpha[1])


def rotatebound_dichotomy_sbest(f,g):
    """ this can be used to plot optimal-skew choices, for degree 0 rotation"""
    def mine(c,t):
        return (lambda v: lognorm_plus_alpha_rot(f,g,c,v), t)
    l=[]
    #l.append(mine(best_l2norm, "L2"))
    #l.append(mine(best_l1norm, "L1"))
    ## For the sup norm, we may use the faster algorithm.
    #h=supnorm_hull_extra(f)
    #l.append((lambda v: logsupnorm_plus_alpha_rot(f,g,v,h), "sup"))
    # l.append(mine(best_supnorm, "sup"))
    l.append(mine(best_l2norm_tk, "L2-int"))
    return l

def rotatebound_dichotomy_sconst(f,g):
    """ this can be used to plot constant-skew choices, for degree 0 rotation"""
    def mine(c,s,t):
        tx = t + " (constant skew)"
        return (lambda v: lognorm_plus_alpha_rot_scons(f,g,c,s,v), tx)
    l=[]
    #l.append(mine(l2norm, skew_l2norm(f), "L2"))
    #l.append(mine(l1norm, skew_l1norm(f), "L1"))
    #l.append(mine(supnorm, skew_supnorm(f), "sup"))
    l.append(mine(l2norm_tk, skew_l2norm_tk(f), "L2-int"))
    return l

def rotatebound_choices(f,g):
    def mine(c,t):
        return (lambda v: lognorm_plus_alpha_rot(f,g,c,v), t)
    def scmine(c,s,t):
        tx = t + " (constant skew)"
        return (lambda v: lognorm_plus_alpha_rot_scons(f,g,c,s,v), tx)
    def sclmine(c,s,t):
        tx = t + " (constant skew, degree 1)"
        return (lambda v: lognorm_plus_alpha_rot_scons_linear(f,g,c,s,v), tx)
    l=[]
    l.append(mine(best_l2norm_tk, "L2-int"))
    l.append(scmine(l2norm_tk, skew_l2norm_tk(f), "L2-int"))
    l.append(sclmine(l2norm_tk, skew_l2norm_tk(f), "L2-int"))
    return l

# Try this:
#               myplot(rotatebound_choices(f,g), 5, 10, 100)

# This shows that degree-1 rotation, as soon as some work margin is
# available, give a very clear win over constant rotation, as expected.

